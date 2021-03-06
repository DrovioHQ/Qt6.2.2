// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_KERBEROS_SECTION_H_
#define CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_KERBEROS_SECTION_H_

#include "base/values.h"
#include "chrome/browser/chromeos/kerberos/kerberos_credentials_manager.h"
#include "chrome/browser/ui/webui/settings/chromeos/os_settings_section.h"

class Profile;

namespace content {
class WebUIDataSource;
}  // namespace content

namespace chromeos {
namespace settings {

class SearchTagRegistry;

// Provides UI strings and search tags for Kerberos settings. Search tags are
// only shown if they are allowed by policy/flags.
class KerberosSection : public OsSettingsSection,
                        public KerberosCredentialsManager::Observer {
 public:
  KerberosSection(Profile* profile,
                  SearchTagRegistry* search_tag_registry,
                  KerberosCredentialsManager* kerberos_credentials_manager);
  ~KerberosSection() override;

 private:
  // OsSettingsSection:
  void AddLoadTimeData(content::WebUIDataSource* html_source) override;
  void AddHandlers(content::WebUI* web_ui) override;
  int GetSectionNameMessageId() const override;
  mojom::Section GetSection() const override;
  mojom::SearchResultIcon GetSectionIcon() const override;
  std::string GetSectionPath() const override;
  bool LogMetric(mojom::Setting setting, base::Value& value) const override;
  void RegisterHierarchy(HierarchyGenerator* generator) const override;

  // KerberosCredentialsManager::Observer:
  void OnKerberosEnabledStateChanged() override;

  KerberosCredentialsManager* kerberos_credentials_manager_;
};

}  // namespace settings
}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_SETTINGS_CHROMEOS_KERBEROS_SECTION_H_
