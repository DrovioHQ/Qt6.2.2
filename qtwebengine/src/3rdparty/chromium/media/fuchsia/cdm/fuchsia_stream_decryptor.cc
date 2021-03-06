// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/fuchsia/cdm/fuchsia_stream_decryptor.h"

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/drm/cpp/fidl.h>

#include "base/bind.h"
#include "base/fuchsia/fuchsia_logging.h"
#include "base/logging.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/decoder_buffer.h"
#include "media/base/decrypt_config.h"
#include "media/base/encryption_pattern.h"
#include "media/base/subsample_entry.h"
#include "media/fuchsia/common/sysmem_buffer_reader.h"
#include "media/fuchsia/common/sysmem_buffer_writer.h"

namespace media {
namespace {

// Minimum number of buffers in the input and output buffer collection.
// Decryptors provided by fuchsia.media.drm API normally decrypt a single
// buffer at a time. Second buffer is useful to allow reading/writing a
// packet while the decryptor is working on another one.
const size_t kMinBufferCount = 2;

std::string GetEncryptionScheme(EncryptionScheme mode) {
  switch (mode) {
    case EncryptionScheme::kCenc:
      return fuchsia::media::ENCRYPTION_SCHEME_CENC;
    case EncryptionScheme::kCbcs:
      return fuchsia::media::ENCRYPTION_SCHEME_CBCS;
    default:
      NOTREACHED() << "unknown encryption mode " << static_cast<int>(mode);
      return "";
  }
}

std::vector<fuchsia::media::SubsampleEntry> GetSubsamples(
    const std::vector<SubsampleEntry>& subsamples) {
  std::vector<fuchsia::media::SubsampleEntry> fuchsia_subsamples(
      subsamples.size());

  for (size_t i = 0; i < subsamples.size(); i++) {
    fuchsia_subsamples[i].clear_bytes = subsamples[i].clear_bytes;
    fuchsia_subsamples[i].encrypted_bytes = subsamples[i].cypher_bytes;
  }

  return fuchsia_subsamples;
}

fuchsia::media::EncryptionPattern GetEncryptionPattern(
    EncryptionPattern pattern) {
  fuchsia::media::EncryptionPattern fuchsia_pattern;
  fuchsia_pattern.clear_blocks = pattern.skip_byte_block();
  fuchsia_pattern.encrypted_blocks = pattern.crypt_byte_block();
  return fuchsia_pattern;
}

fuchsia::media::FormatDetails GetClearFormatDetails() {
  fuchsia::media::EncryptedFormat encrypted_format;
  encrypted_format.set_scheme(fuchsia::media::ENCRYPTION_SCHEME_UNENCRYPTED)
      .set_subsamples({})
      .set_init_vector({});

  fuchsia::media::FormatDetails format;
  format.set_format_details_version_ordinal(0);
  format.mutable_domain()->crypto().set_encrypted(std::move(encrypted_format));
  return format;
}

fuchsia::media::FormatDetails GetEncryptedFormatDetails(
    const DecryptConfig* config) {
  DCHECK(config);

  fuchsia::media::EncryptedFormat encrypted_format;
  encrypted_format.set_scheme(GetEncryptionScheme(config->encryption_scheme()))
      .set_key_id(std::vector<uint8_t>(config->key_id().begin(),
                                       config->key_id().end()))
      .set_init_vector(
          std::vector<uint8_t>(config->iv().begin(), config->iv().end()))
      .set_subsamples(GetSubsamples(config->subsamples()));
  if (config->encryption_scheme() == EncryptionScheme::kCbcs) {
    DCHECK(config->encryption_pattern().has_value());
    encrypted_format.set_pattern(
        GetEncryptionPattern(config->encryption_pattern().value()));
  }

  fuchsia::media::FormatDetails format;
  format.set_format_details_version_ordinal(0);
  format.mutable_domain()->crypto().set_encrypted(std::move(encrypted_format));
  return format;
}

}  // namespace

FuchsiaStreamDecryptorBase::FuchsiaStreamDecryptorBase(
    fuchsia::media::StreamProcessorPtr processor,
    size_t min_buffer_size)
    : processor_(std::move(processor), this),
      min_buffer_size_(min_buffer_size),
      allocator_("CrFuchsiaStreamDecryptorBase") {}

FuchsiaStreamDecryptorBase::~FuchsiaStreamDecryptorBase() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

int FuchsiaStreamDecryptorBase::GetMaxDecryptRequests() const {
  return input_writer_queue_.num_buffers() + 1;
}

void FuchsiaStreamDecryptorBase::DecryptInternal(
    scoped_refptr<DecoderBuffer> encrypted) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  input_writer_queue_.EnqueueBuffer(std::move(encrypted));
}

void FuchsiaStreamDecryptorBase::ResetStream() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Close current stream and drop all the cached decoder buffers.
  // Keep input and output buffers to avoid buffer re-allocation.
  processor_.Reset();
  input_writer_queue_.ResetQueue();
}

// StreamProcessorHelper::Client implementation:
void FuchsiaStreamDecryptorBase::AllocateInputBuffers(
    const fuchsia::media::StreamBufferConstraints& stream_constraints) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::Optional<fuchsia::sysmem::BufferCollectionConstraints>
      buffer_constraints = SysmemBufferWriter::GetRecommendedConstraints(
          kMinBufferCount, min_buffer_size_);

  if (!buffer_constraints.has_value()) {
    OnError();
    return;
  }

  input_pool_creator_ =
      allocator_.MakeBufferPoolCreator(1 /* num_shared_token */);

  input_pool_creator_->Create(
      std::move(buffer_constraints).value(),
      base::BindOnce(&FuchsiaStreamDecryptorBase::OnInputBufferPoolCreated,
                     base::Unretained(this)));
}

void FuchsiaStreamDecryptorBase::OnOutputFormat(
    fuchsia::media::StreamOutputFormat format) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void FuchsiaStreamDecryptorBase::OnInputBufferPoolCreated(
    std::unique_ptr<SysmemBufferPool> pool) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!pool) {
    DLOG(ERROR) << "Fail to allocate input buffer.";
    OnError();
    return;
  }

  input_pool_ = std::move(pool);

  // Provide token before enabling writer. Tokens must be provided to
  // StreamProcessor before getting the allocated buffers.
  processor_.CompleteInputBuffersAllocation(input_pool_->TakeToken());

  input_pool_->CreateWriter(base::BindOnce(
      &FuchsiaStreamDecryptorBase::OnWriterCreated, base::Unretained(this)));
}

void FuchsiaStreamDecryptorBase::OnWriterCreated(
    std::unique_ptr<SysmemBufferWriter> writer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!writer) {
    OnError();
    return;
  }

  input_writer_queue_.Start(
      std::move(writer),
      base::BindRepeating(&FuchsiaStreamDecryptorBase::SendInputPacket,
                          base::Unretained(this)),
      base::BindRepeating(&FuchsiaStreamDecryptorBase::ProcessEndOfStream,
                          base::Unretained(this)));
}

void FuchsiaStreamDecryptorBase::SendInputPacket(
    const DecoderBuffer* buffer,
    StreamProcessorHelper::IoPacket packet) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!packet.unit_end()) {
    // The encrypted data size is too big. Decryptor should consider
    // splitting the buffer and update the IV and subsample entries.
    // TODO(crbug.com/1003651): Handle large encrypted buffer correctly. For
    // now, just reject the decryption.
    LOG(ERROR) << "DecoderBuffer doesn't fit in one packet.";
    OnError();
    return;
  }

  fuchsia::media::FormatDetails format =
      (buffer->decrypt_config())
          ? GetEncryptedFormatDetails(buffer->decrypt_config())
          : GetClearFormatDetails();

  packet.set_format(std::move(format));
  processor_.Process(std::move(packet));
}

void FuchsiaStreamDecryptorBase::ProcessEndOfStream() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  processor_.ProcessEos();
}

FuchsiaClearStreamDecryptor::FuchsiaClearStreamDecryptor(
    fuchsia::media::StreamProcessorPtr processor,
    size_t min_buffer_size)
    : FuchsiaStreamDecryptorBase(std::move(processor), min_buffer_size) {}

FuchsiaClearStreamDecryptor::~FuchsiaClearStreamDecryptor() = default;

void FuchsiaClearStreamDecryptor::Decrypt(
    scoped_refptr<DecoderBuffer> encrypted,
    Decryptor::DecryptCB decrypt_cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!decrypt_cb_);

  decrypt_cb_ = std::move(decrypt_cb);
  current_status_ = Decryptor::kSuccess;
  DecryptInternal(std::move(encrypted));
}

void FuchsiaClearStreamDecryptor::CancelDecrypt() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ResetStream();

  // Fire |decrypt_cb_| immediately as required by Decryptor::CancelDecrypt.
  if (decrypt_cb_)
    std::move(decrypt_cb_).Run(Decryptor::kSuccess, nullptr);
}

void FuchsiaClearStreamDecryptor::AllocateOutputBuffers(
    const fuchsia::media::StreamBufferConstraints& stream_constraints) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!stream_constraints.has_packet_count_for_client_max() ||
      !stream_constraints.has_packet_count_for_client_min()) {
    DLOG(ERROR) << "StreamBufferConstraints doesn't contain required fields.";
    OnError();
    return;
  }

  output_pool_creator_ =
      allocator_.MakeBufferPoolCreator(1 /* num_shared_token */);
  output_pool_creator_->Create(
      SysmemBufferReader::GetRecommendedConstraints(kMinBufferCount,
                                                    min_buffer_size_),
      base::BindOnce(&FuchsiaClearStreamDecryptor::OnOutputBufferPoolCreated,
                     base::Unretained(this)));
}

void FuchsiaClearStreamDecryptor::OnProcessEos() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Decryptor never pushes EOS frame.
  NOTREACHED();
}

void FuchsiaClearStreamDecryptor::OnOutputPacket(
    StreamProcessorHelper::IoPacket packet) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(decrypt_cb_);

  DCHECK(output_reader_);
  if (!output_pool_->is_live()) {
    DLOG(ERROR) << "Output buffer pool is dead.";
    return;
  }

  // If that's not the last packet for the current Decrypt() request then just
  // store the output and wait for the next packet.
  if (!packet.unit_end()) {
    size_t pos = output_data_.size();
    output_data_.resize(pos + packet.size());

    bool read_success = output_reader_->Read(
        packet.buffer_index(), packet.offset(),
        base::make_span(output_data_.data() + pos, packet.size()));

    if (!read_success) {
      // If we've failed to read a partial packet then delay reporting the error
      // until we've received the last packet to make sure we consume all output
      // packets generated by the last Decrypt() call.
      DLOG(ERROR) << "Fail to get decrypted result.";
      current_status_ = Decryptor::kError;
      output_data_.clear();
    }

    return;
  }

  // We've received the last packet. Assemble DecoderBuffer and pass it to the
  // DecryptCB.
  auto clear_buffer =
      base::MakeRefCounted<DecoderBuffer>(output_data_.size() + packet.size());
  clear_buffer->set_timestamp(packet.timestamp());

  // Copy data received in the previous packets.
  memcpy(clear_buffer->writable_data(), output_data_.data(),
         output_data_.size());
  output_data_.clear();

  // Copy data received in the last packet
  bool read_success = output_reader_->Read(
      packet.buffer_index(), packet.offset(),
      base::make_span(clear_buffer->writable_data() + output_data_.size(),
                      packet.size()));

  if (!read_success) {
    DLOG(ERROR) << "Fail to get decrypted result.";
    current_status_ = Decryptor::kError;
  }

  std::move(decrypt_cb_)
      .Run(current_status_, current_status_ == Decryptor::kSuccess
                                ? std::move(clear_buffer)
                                : nullptr);
}

void FuchsiaClearStreamDecryptor::OnNoKey() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Reset the queue. The client is expected to call Decrypt() with the same
  // buffer again when it gets kNoKey.
  input_writer_queue_.ResetQueue();

  if (decrypt_cb_)
    std::move(decrypt_cb_).Run(Decryptor::kNoKey, nullptr);
}

void FuchsiaClearStreamDecryptor::OnError() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ResetStream();
  if (decrypt_cb_)
    std::move(decrypt_cb_).Run(Decryptor::kError, nullptr);
}

void FuchsiaClearStreamDecryptor::OnOutputBufferPoolCreated(
    std::unique_ptr<SysmemBufferPool> pool) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!pool) {
    LOG(ERROR) << "Fail to allocate output buffer.";
    OnError();
    return;
  }

  output_pool_ = std::move(pool);

  // Provide token before enabling reader. Tokens must be provided to
  // StreamProcessor before getting the allocated buffers.
  processor_.CompleteOutputBuffersAllocation(output_pool_->TakeToken());

  output_pool_->CreateReader(base::BindOnce(
      &FuchsiaClearStreamDecryptor::OnOutputBufferPoolReaderCreated,
      base::Unretained(this)));
}

void FuchsiaClearStreamDecryptor::OnOutputBufferPoolReaderCreated(
    std::unique_ptr<SysmemBufferReader> reader) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!reader) {
    LOG(ERROR) << "Fail to enable output buffer reader.";
    OnError();
    return;
  }

  DCHECK(!output_reader_);
  output_reader_ = std::move(reader);
}

FuchsiaSecureStreamDecryptor::FuchsiaSecureStreamDecryptor(
    fuchsia::media::StreamProcessorPtr processor,
    Client* client)
    : FuchsiaStreamDecryptorBase(std::move(processor),
                                 client->GetInputBufferSize()),
      client_(client) {}

FuchsiaSecureStreamDecryptor::~FuchsiaSecureStreamDecryptor() = default;

void FuchsiaSecureStreamDecryptor::SetOutputBufferCollectionToken(
    fuchsia::sysmem::BufferCollectionTokenPtr token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!complete_buffer_allocation_callback_);
  complete_buffer_allocation_callback_ =
      base::BindOnce(&StreamProcessorHelper::CompleteOutputBuffersAllocation,
                     base::Unretained(&processor_), std::move(token));
  if (waiting_output_buffers_) {
    std::move(complete_buffer_allocation_callback_).Run();
    waiting_output_buffers_ = false;
  }
}

void FuchsiaSecureStreamDecryptor::Decrypt(
    scoped_refptr<DecoderBuffer> encrypted) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DecryptInternal(std::move(encrypted));
}

void FuchsiaSecureStreamDecryptor::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ResetStream();
  waiting_for_key_ = false;
}

void FuchsiaSecureStreamDecryptor::AllocateOutputBuffers(
    const fuchsia::media::StreamBufferConstraints& stream_constraints) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (complete_buffer_allocation_callback_) {
    std::move(complete_buffer_allocation_callback_).Run();
  } else {
    waiting_output_buffers_ = true;
  }
}

void FuchsiaSecureStreamDecryptor::OnProcessEos() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  client_->OnDecryptorEndOfStreamPacket();
}

void FuchsiaSecureStreamDecryptor::OnOutputPacket(
    StreamProcessorHelper::IoPacket packet) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  client_->OnDecryptorOutputPacket(std::move(packet));
}

base::RepeatingClosure FuchsiaSecureStreamDecryptor::GetOnNewKeyClosure() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return BindToCurrentLoop(base::BindRepeating(
      &FuchsiaSecureStreamDecryptor::OnNewKey, weak_factory_.GetWeakPtr()));
}

void FuchsiaSecureStreamDecryptor::OnError() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  ResetStream();

  // No need to reset other fields since OnError() is called for non-recoverable
  // errors.

  client_->OnDecryptorError();
}

void FuchsiaSecureStreamDecryptor::OnNoKey() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!waiting_for_key_);

  // Reset stream position, but keep all pending buffers. They will be
  // resubmitted later, when we have a new key.
  input_writer_queue_.ResetPositionAndPause();

  if (retry_on_no_key_) {
    retry_on_no_key_ = false;
    input_writer_queue_.Unpause();
    return;
  }

  waiting_for_key_ = true;
  client_->OnDecryptorNoKey();
}

void FuchsiaSecureStreamDecryptor::OnNewKey() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!waiting_for_key_) {
    retry_on_no_key_ = true;
    return;
  }

  DCHECK(!retry_on_no_key_);
  waiting_for_key_ = false;
  input_writer_queue_.Unpause();
}

}  // namespace media
