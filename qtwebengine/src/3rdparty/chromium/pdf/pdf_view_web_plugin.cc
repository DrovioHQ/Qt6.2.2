// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pdf/pdf_view_web_plugin.h"

#include <stddef.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/check_op.h"
#include "base/location.h"
#include "base/notreached.h"
#include "base/thread_annotations.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "base/values.h"
#include "cc/paint/paint_canvas.h"
#include "net/cookies/site_for_cookies.h"
#include "pdf/accessibility_structs.h"
#include "pdf/pdf_engine.h"
#include "pdf/pdf_init.h"
#include "pdf/pdfium/pdfium_engine.h"
#include "pdf/post_message_receiver.h"
#include "pdf/ppapi_migration/bitmap.h"
#include "pdf/ppapi_migration/graphics.h"
#include "pdf/ppapi_migration/url_loader.h"
#include "ppapi/c/pp_errors.h"
#include "third_party/blink/public/common/input/web_coalesced_input_event.h"
#include "third_party/blink/public/common/metrics/document_update_reason.h"
#include "third_party/blink/public/mojom/input/focus_type.mojom-shared.h"
#include "third_party/blink/public/platform/web_input_event_result.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/platform/web_url.h"
#include "third_party/blink/public/platform/web_url_error.h"
#include "third_party/blink/public/platform/web_url_request.h"
#include "third_party/blink/public/platform/web_url_response.h"
#include "third_party/blink/public/web/web_associated_url_loader.h"
#include "third_party/blink/public/web/web_associated_url_loader_options.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_plugin_container.h"
#include "third_party/blink/public/web/web_plugin_params.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/cursor/cursor.h"
#include "ui/gfx/skia_util.h"
#include "v8/include/v8.h"

namespace chrome_pdf {

namespace {

// Initialization performed per renderer process. Initialization may be
// triggered from multiple plugin instances, but should only execute once.
//
// TODO(crbug.com/1123621): We may be able to simplify this once we've figured
// out exactly which processes need to initialize and shutdown PDFium.
class PerProcessInitializer final {
 public:
  static PerProcessInitializer& GetInstance() {
    static PerProcessInitializer instance;
    return instance;
  }

  void Acquire() {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

    DCHECK_GE(init_count_, 0);
    if (init_count_++ > 0)
      return;

    DCHECK(!IsSDKInitializedViaPlugin());
    // TODO(crbug.com/1111024): Support JavaScript.
    InitializeSDK(/*enable_v8=*/false);
    SetIsSDKInitializedViaPlugin(true);
  }

  void Release() {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

    DCHECK_GT(init_count_, 0);
    if (--init_count_ > 0)
      return;

    DCHECK(IsSDKInitializedViaPlugin());
    ShutdownSDK();
    SetIsSDKInitializedViaPlugin(false);
  }

 private:
  int init_count_ GUARDED_BY_CONTEXT(thread_checker_) = 0;

  // TODO(crbug.com/1123731): Assuming PDFium is thread-hostile for now, and
  // must use one thread exclusively.
  THREAD_CHECKER(thread_checker_);
};

}  // namespace

PdfViewWebPlugin::PdfViewWebPlugin(const blink::WebPluginParams& params)
    : initial_params_(params) {}

PdfViewWebPlugin::~PdfViewWebPlugin() = default;

// Modeled on `OutOfProcessInstance::Init()`.
bool PdfViewWebPlugin::Initialize(blink::WebPluginContainer* container) {
  DCHECK_EQ(container->Plugin(), this);
  container_ = container;

  std::string stream_url;
  for (size_t i = 0; i < initial_params_.attribute_names.size(); ++i) {
    if (initial_params_.attribute_names[i] == "stream-url") {
      stream_url = initial_params_.attribute_values[i].Utf8();
    } else if (initial_params_.attribute_names[i] == "background-color") {
      SkColor background_color;
      if (!base::StringToUint(initial_params_.attribute_values[i].Utf8(),
                              &background_color)) {
        return false;
      }
      SetBackgroundColor(background_color);
    }
  }

  // Contents of `initial_params_` no longer needed.
  initial_params_ = {};

  PerProcessInitializer::GetInstance().Acquire();
  InitializeEngine(PDFiumFormFiller::ScriptOption::kNoJavaScript);
  LoadUrl(stream_url, /*is_print_preview=*/false);
  post_message_sender_.set_container(container_);
  return true;
}

void PdfViewWebPlugin::Destroy() {
  if (container_) {
    // Explicitly destroy the PDFEngine during destruction as it may call back
    // into this object.
    DestroyEngine();
    PerProcessInitializer::GetInstance().Release();
  }

  container_ = nullptr;
  post_message_sender_.set_container(nullptr);

  delete this;
}

blink::WebPluginContainer* PdfViewWebPlugin::Container() const {
  return container_;
}

v8::Local<v8::Object> PdfViewWebPlugin::V8ScriptableObject(
    v8::Isolate* isolate) {
  if (scriptable_receiver_.IsEmpty()) {
    // TODO(crbug.com/1123731): Messages should not be handled on the renderer
    // main thread.
    scriptable_receiver_.Reset(
        isolate,
        PostMessageReceiver::Create(isolate, weak_factory_.GetWeakPtr(),
                                    base::SequencedTaskRunnerHandle::Get()));
  }

  return scriptable_receiver_.Get(isolate);
}

void PdfViewWebPlugin::UpdateAllLifecyclePhases(
    blink::DocumentUpdateReason reason) {}

void PdfViewWebPlugin::Paint(cc::PaintCanvas* canvas, const gfx::Rect& rect) {}

void PdfViewWebPlugin::UpdateGeometry(const gfx::Rect& window_rect,
                                      const gfx::Rect& clip_rect,
                                      const gfx::Rect& unobscured_rect,
                                      bool is_visible) {}

void PdfViewWebPlugin::UpdateFocus(bool focused,
                                   blink::mojom::FocusType focus_type) {}

void PdfViewWebPlugin::UpdateVisibility(bool visibility) {}

blink::WebInputEventResult PdfViewWebPlugin::HandleInputEvent(
    const blink::WebCoalescedInputEvent& event,
    ui::Cursor* cursor) {
  return blink::WebInputEventResult::kNotHandled;
}

void PdfViewWebPlugin::DidReceiveResponse(
    const blink::WebURLResponse& response) {}

void PdfViewWebPlugin::DidReceiveData(const char* data, size_t data_length) {}

void PdfViewWebPlugin::DidFinishLoading() {}

void PdfViewWebPlugin::DidFailLoading(const blink::WebURLError& error) {}

void PdfViewWebPlugin::ProposeDocumentLayout(const DocumentLayout& layout) {}

void PdfViewWebPlugin::DidScroll(const gfx::Vector2d& offset) {}

void PdfViewWebPlugin::ScrollToX(int x_in_screen_coords) {}

void PdfViewWebPlugin::ScrollToY(int y_in_screen_coords) {}

void PdfViewWebPlugin::ScrollBy(const gfx::Vector2d& scroll_delta) {}

void PdfViewWebPlugin::ScrollToPage(int page) {}

void PdfViewWebPlugin::NavigateTo(const std::string& url,
                                  WindowOpenDisposition disposition) {}

void PdfViewWebPlugin::NavigateToDestination(int page,
                                             const float* x,
                                             const float* y,
                                             const float* zoom) {}

void PdfViewWebPlugin::UpdateCursor(PP_CursorType_Dev cursor) {}

void PdfViewWebPlugin::UpdateTickMarks(
    const std::vector<gfx::Rect>& tickmarks) {}

void PdfViewWebPlugin::NotifyNumberOfFindResultsChanged(int total,
                                                        bool final_result) {}

void PdfViewWebPlugin::NotifySelectedFindResultChanged(int current_find_index) {
}

void PdfViewWebPlugin::NotifyTouchSelectionOccurred() {}

void PdfViewWebPlugin::GetDocumentPassword(
    base::OnceCallback<void(const std::string&)> callback) {}

void PdfViewWebPlugin::Beep() {}

void PdfViewWebPlugin::Alert(const std::string& message) {}

bool PdfViewWebPlugin::Confirm(const std::string& message) {
  return false;
}

std::string PdfViewWebPlugin::Prompt(const std::string& question,
                                     const std::string& default_answer) {
  return "";
}

std::string PdfViewWebPlugin::GetURL() {
  return "";
}

void PdfViewWebPlugin::Email(const std::string& to,
                             const std::string& cc,
                             const std::string& bcc,
                             const std::string& subject,
                             const std::string& body) {}

void PdfViewWebPlugin::Print() {}

void PdfViewWebPlugin::SubmitForm(const std::string& url,
                                  const void* data,
                                  int length) {}

std::unique_ptr<UrlLoader> PdfViewWebPlugin::CreateUrlLoader() {
  return nullptr;
}

std::vector<PDFEngine::Client::SearchStringResult>
PdfViewWebPlugin::SearchString(const base::char16* string,
                               const base::char16* term,
                               bool case_sensitive) {
  return {};
}

void PdfViewWebPlugin::DocumentLoadComplete() {
  NOTIMPLEMENTED();
}

void PdfViewWebPlugin::DocumentLoadFailed() {
  NOTIMPLEMENTED();
}

pp::Instance* PdfViewWebPlugin::GetPluginInstance() {
  return nullptr;
}

void PdfViewWebPlugin::DocumentHasUnsupportedFeature(
    const std::string& feature) {}

void PdfViewWebPlugin::DocumentLoadProgress(uint32_t available,
                                            uint32_t doc_size) {}

void PdfViewWebPlugin::FormTextFieldFocusChange(bool in_focus) {}

bool PdfViewWebPlugin::IsPrintPreview() {
  return false;
}

void PdfViewWebPlugin::IsSelectingChanged(bool is_selecting) {}

void PdfViewWebPlugin::SelectionChanged(const gfx::Rect& left,
                                        const gfx::Rect& right) {}

void PdfViewWebPlugin::EnteredEditMode() {}

void PdfViewWebPlugin::DocumentFocusChanged(bool document_has_focus) {}

void PdfViewWebPlugin::SetSelectedText(const std::string& selected_text) {
  NOTIMPLEMENTED();
}

void PdfViewWebPlugin::SetLinkUnderCursor(
    const std::string& link_under_cursor) {
  NOTIMPLEMENTED();
}

bool PdfViewWebPlugin::IsValidLink(const std::string& url) {
  return base::Value(url).is_string();
}

std::unique_ptr<Graphics> PdfViewWebPlugin::CreatePaintGraphics(
    const gfx::Size& size) {
  auto graphics = SkiaGraphics::Create(size);
  DCHECK(graphics);
  return graphics;
}

bool PdfViewWebPlugin::BindPaintGraphics(Graphics& graphics) {
  NOTIMPLEMENTED_LOG_ONCE();
  return false;
}

void PdfViewWebPlugin::ScheduleTaskOnMainThread(
    base::TimeDelta delay,
    ResultCallback callback,
    int32_t result,
    const base::Location& from_here) {
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      from_here, base::BindOnce(std::move(callback), result), delay);
}

bool PdfViewWebPlugin::IsValid() const {
  return container_ && container_->GetDocument().GetFrame();
}

blink::WebURL PdfViewWebPlugin::CompleteURL(
    const blink::WebString& partial_url) const {
  DCHECK(IsValid());
  return container_->GetDocument().CompleteURL(partial_url);
}

net::SiteForCookies PdfViewWebPlugin::SiteForCookies() const {
  DCHECK(IsValid());
  return container_->GetDocument().SiteForCookies();
}

void PdfViewWebPlugin::SetReferrerForRequest(
    blink::WebURLRequest& request,
    const blink::WebURL& referrer_url) {
  DCHECK(IsValid());
  container_->GetDocument().GetFrame()->SetReferrerForRequest(request,
                                                              referrer_url);
}

std::unique_ptr<blink::WebAssociatedURLLoader>
PdfViewWebPlugin::CreateAssociatedURLLoader(
    const blink::WebAssociatedURLLoaderOptions& options) {
  DCHECK(IsValid());
  return container_->GetDocument().GetFrame()->CreateAssociatedURLLoader(
      options);
}

void PdfViewWebPlugin::OnMessage(const base::Value& message) {
  PdfViewPluginBase::HandleMessage(message);
}

base::WeakPtr<PdfViewPluginBase> PdfViewWebPlugin::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

std::unique_ptr<UrlLoader> PdfViewWebPlugin::CreateUrlLoaderInternal() {
  auto loader = std::make_unique<BlinkUrlLoader>(weak_factory_.GetWeakPtr());
  loader->GrantUniversalAccess();
  return loader;
}

// Modeled on `OutOfProcessInstance::DidOpen()`.
void PdfViewWebPlugin::DidOpen(std::unique_ptr<UrlLoader> loader,
                               int32_t result) {
  if (result == PP_OK) {
    if (!engine()->HandleDocumentLoad(std::move(loader)))
      DocumentLoadFailed();
  } else {
    NOTIMPLEMENTED();
  }
}

void PdfViewWebPlugin::DidOpenPreview(std::unique_ptr<UrlLoader> loader,
                                      int32_t result) {
  NOTIMPLEMENTED();
}

void PdfViewWebPlugin::SendMessage(base::Value message) {
  post_message_sender_.Post(std::move(message));
}

void PdfViewWebPlugin::InitImageData(const gfx::Size& size) {
  mutable_image_data() = CreateN32PremulSkBitmap(gfx::SizeToSkISize(size));
}

// TODO(https://crbug.com/1144444): Add a Pepper-free implementation to set
// accessibility document information.
void PdfViewWebPlugin::SetAccessibilityDocInfo(
    const AccessibilityDocInfo& doc_info) {
  NOTIMPLEMENTED();
}

// TODO(https://crbug.com/1144444): Add a Pepper-free implementation to set
// accessibility page information.
void PdfViewWebPlugin::SetAccessibilityPageInfo(
    AccessibilityPageInfo page_info,
    std::vector<AccessibilityTextRunInfo> text_runs,
    std::vector<AccessibilityCharInfo> chars,
    AccessibilityPageObjects page_objects) {
  NOTIMPLEMENTED();
}

// TODO(https://crbug.com/1144444): Add a Pepper-free implementation to set
// accessibility viewport information.
void PdfViewWebPlugin::SetAccessibilityViewportInfo(
    const AccessibilityViewportInfo& viewport_info) {
  NOTIMPLEMENTED();
}

void PdfViewWebPlugin::OnViewportChanged(gfx::Rect view_rect,
                                         float new_device_scale) {
  UpdateGeometryOnViewChanged(view_rect, new_device_scale);

  // TODO(http://crbug.com/1099020): Update scroll position for painting the
  // print preview plugin.
}

}  // namespace chrome_pdf
