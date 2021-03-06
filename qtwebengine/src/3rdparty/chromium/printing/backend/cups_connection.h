// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTING_BACKEND_CUPS_CONNECTION_H_
#define PRINTING_BACKEND_CUPS_CONNECTION_H_

#include <cups/cups.h>

#include <memory>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "printing/backend/cups_deleters.h"
#include "printing/backend/cups_jobs.h"
#include "printing/backend/cups_printer.h"
#include "printing/printer_status.h"
#include "printing/printing_export.h"
#include "url/gurl.h"

namespace printing {

// Represents the status of a printer queue.
struct PRINTING_EXPORT QueueStatus {
  QueueStatus();
  QueueStatus(const QueueStatus& other);
  ~QueueStatus();

  PrinterStatus printer_status;
  std::vector<CupsJob> jobs;
};

// Represents a connection to a CUPS server.
class PRINTING_EXPORT CupsConnection {
 public:
  virtual ~CupsConnection() = default;

  static std::unique_ptr<CupsConnection> Create(const GURL& print_server_url,
                                                http_encryption_t encryption,
                                                bool blocking);

  // Returns a vector of all the printers configure on the CUPS server.
  virtual std::vector<std::unique_ptr<CupsPrinter>> GetDests() = 0;

  // Returns a printer for |printer_name| from the connected server.
  virtual std::unique_ptr<CupsPrinter> GetPrinter(
      const std::string& printer_name) = 0;

  // Queries CUPS for printer queue status for |printer_ids|.  Populates |jobs|
  // with said information with one QueueStatus per printer_id.  Returns true if
  // all the queries were successful.  In the event of failure, |jobs| will be
  // unchanged.
  virtual bool GetJobs(const std::vector<std::string>& printer_ids,
                       std::vector<QueueStatus>* jobs) = 0;

  // Queries CUPS for printer status for |printer_id|.
  // Returns true if the query was successful.
  virtual bool GetPrinterStatus(const std::string& printer_id,
                                PrinterStatus* printer_status) = 0;

  virtual std::string server_name() const = 0;

  virtual int last_error() const = 0;
};

}  // namespace printing

#endif  // PRINTING_BACKEND_CUPS_CONNECTION_H_
