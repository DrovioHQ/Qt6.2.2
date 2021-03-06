// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/tab_search/tab_search_page_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/trace_event/trace_event.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/tabs/tab_renderer_data.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/webui/util/image_util.h"

namespace {
constexpr base::TimeDelta kTabsChangeDelay =
    base::TimeDelta::FromMilliseconds(50);

#if BUILDFLAG(IS_CHROMEOS_ASH)
constexpr char kFeedbackCategoryTag[] = "FromTabSearch";
#else
constexpr char kFeedbackCategoryTag[] = "FromTabSearchBrowser";
#endif
}

TabSearchPageHandler::TabSearchPageHandler(
    mojo::PendingReceiver<tab_search::mojom::PageHandler> receiver,
    mojo::PendingRemote<tab_search::mojom::Page> page,
    content::WebUI* web_ui,
    ui::MojoBubbleWebUIController* webui_controller)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      web_ui_(web_ui),
      webui_controller_(webui_controller),
      debounce_timer_(std::make_unique<base::RetainingOneShotTimer>(
          FROM_HERE,
          kTabsChangeDelay,
          base::BindRepeating(&TabSearchPageHandler::NotifyTabsChanged,
                              base::Unretained(this)))) {
  Observe(web_ui_->GetWebContents());
  browser_tab_strip_tracker_.Init();
}

TabSearchPageHandler::~TabSearchPageHandler() {
  base::UmaHistogramCounts1000("Tabs.TabSearch.NumTabsClosedPerInstance",
                               num_tabs_closed_);
  base::UmaHistogramEnumeration("Tabs.TabSearch.CloseAction",
                                called_switch_to_tab_
                                    ? TabSearchCloseAction::kTabSwitch
                                    : TabSearchCloseAction::kNoAction);
}

void TabSearchPageHandler::CloseTab(int32_t tab_id) {
  base::Optional<TabDetails> optional_details = GetTabDetails(tab_id);
  if (!optional_details)
    return;

  ++num_tabs_closed_;

  // CloseTab() can target the WebContents hosting Tab Search if the Tab Search
  // WebUI is open in a chrome browser tab rather than its bubble. In this case
  // CloseWebContentsAt() closes the WebContents hosting this
  // TabSearchPageHandler object, causing it to be immediately destroyed. Ensure
  // that no further actions are performed following the call to
  // CloseWebContentsAt(). See (https://crbug.com/1175507).
  auto* tab_strip_model = optional_details->tab_strip_model;
  const int tab_index = optional_details->index;
  tab_strip_model->CloseWebContentsAt(
      tab_index, TabStripModel::CLOSE_CREATE_HISTORICAL_TAB);
  // Do not add code past this point.
}

void TabSearchPageHandler::GetProfileData(GetProfileDataCallback callback) {
  TRACE_EVENT0("browser", "custom_metric:TabSearchPageHandler:GetProfileTabs");
  auto profile_tabs = CreateProfileData();
  // On first run record the number of windows and tabs open for the given
  // profile.
  if (!sent_initial_payload_) {
    sent_initial_payload_ = true;
    int tab_count = 0;
    for (const auto& window : profile_tabs->windows)
      tab_count += window->tabs.size();
    base::UmaHistogramCounts100("Tabs.TabSearch.NumWindowsOnOpen",
                                profile_tabs->windows.size());
    base::UmaHistogramCounts10000("Tabs.TabSearch.NumTabsOnOpen", tab_count);
  }

  std::move(callback).Run(std::move(profile_tabs));
}

base::Optional<TabSearchPageHandler::TabDetails>
TabSearchPageHandler::GetTabDetails(int32_t tab_id) {
  for (auto* browser : *BrowserList::GetInstance()) {
    if (!ShouldTrackBrowser(browser)) {
      continue;
    }

    TabStripModel* tab_strip_model = browser->tab_strip_model();
    for (int index = 0; index < tab_strip_model->count(); ++index) {
      content::WebContents* contents = tab_strip_model->GetWebContentsAt(index);
      if (extensions::ExtensionTabUtil::GetTabId(contents) == tab_id) {
        return TabDetails(browser, tab_strip_model, index);
      }
    }
  }

  return base::nullopt;
}

void TabSearchPageHandler::GetTabGroups(GetTabGroupsCallback callback) {
  // TODO(crbug.com/1096120): Implement this when we can get theme color from
  // browser
  NOTIMPLEMENTED();
}

void TabSearchPageHandler::ShowFeedbackPage() {
  Browser* browser = chrome::FindLastActive();
  if (!browser)
    return;
  chrome::ShowFeedbackPage(browser,
                           chrome::FeedbackSource::kFeedbackSourceTabSearch,
                           std::string() /* description_template */,
                           std::string() /* description_placeholder_text */,
                           std::string(kFeedbackCategoryTag) /* category_tag */,
                           std::string() /* extra_diagnostics */);
}

void TabSearchPageHandler::SwitchToTab(
    tab_search::mojom::SwitchToTabInfoPtr switch_to_tab_info) {
  base::Optional<TabDetails> optional_details =
      GetTabDetails(switch_to_tab_info->tab_id);
  if (!optional_details)
    return;

  called_switch_to_tab_ = true;

  const TabDetails& details = optional_details.value();
  details.tab_strip_model->ActivateTabAt(details.index);
  details.browser->window()->Activate();
}

void TabSearchPageHandler::ShowUI() {
  auto embedder = webui_controller_->embedder();
  if (embedder)
    embedder->ShowUI();
}

tab_search::mojom::ProfileDataPtr TabSearchPageHandler::CreateProfileData() {
  auto profile_data = tab_search::mojom::ProfileData::New();
  Browser* active_browser = chrome::FindLastActive();
  if (!active_browser)
    return profile_data;
  for (auto* browser : *BrowserList::GetInstance()) {
    if (!ShouldTrackBrowser(browser))
      continue;
    TabStripModel* tab_strip_model = browser->tab_strip_model();
    auto window = tab_search::mojom::Window::New();
    window->active = (browser == active_browser);
    window->height = browser->window()->GetContentsSize().height();
    for (int i = 0; i < tab_strip_model->count(); ++i) {
      window->tabs.push_back(
          GetTabData(tab_strip_model, tab_strip_model->GetWebContentsAt(i), i));
    }
    profile_data->windows.push_back(std::move(window));
  }
  return profile_data;
}

tab_search::mojom::TabPtr TabSearchPageHandler::GetTabData(
    TabStripModel* tab_strip_model,
    content::WebContents* contents,
    int index) {
  auto tab_data = tab_search::mojom::Tab::New();

  tab_data->active = tab_strip_model->active_index() == index;
  tab_data->tab_id = extensions::ExtensionTabUtil::GetTabId(contents);
  tab_data->index = index;
  const base::Optional<tab_groups::TabGroupId> group_id =
      tab_strip_model->GetTabGroupForTab(index);
  if (group_id.has_value()) {
    tab_data->group_id = group_id.value().ToString();
  }
  TabRendererData tab_renderer_data =
      TabRendererData::FromTabInModel(tab_strip_model, index);
  tab_data->pinned = tab_renderer_data.pinned;
  tab_data->title = base::UTF16ToUTF8(tab_renderer_data.title);
  tab_data->url = tab_renderer_data.visible_url.spec();

  if (tab_renderer_data.favicon.isNull()) {
    tab_data->is_default_favicon = true;
  } else {
    tab_data->favicon_url = webui::EncodePNGAndMakeDataURI(
        tab_renderer_data.favicon, web_ui_->GetDeviceScaleFactor());
    tab_data->is_default_favicon =
        tab_renderer_data.favicon.BackedBySameObjectAs(
            favicon::GetDefaultFavicon().AsImageSkia());
  }

  tab_data->show_icon = tab_renderer_data.show_icon;
  tab_data->last_active_time_ticks = contents->GetLastActiveTime();

  return tab_data;
}

void TabSearchPageHandler::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (webui_hidden_ ||
      browser_tab_strip_tracker_.is_processing_initial_browsers()) {
    return;
  }
  if (change.type() == TabStripModelChange::kRemoved) {
    std::vector<int> tab_ids;
    for (auto& content_with_index : change.GetRemove()->contents) {
      tab_ids.push_back(
          extensions::ExtensionTabUtil::GetTabId(content_with_index.contents));
    }
    page_->TabsRemoved(tab_ids);
    return;
  }
  ScheduleDebounce();
}

void TabSearchPageHandler::TabChangedAt(content::WebContents* contents,
                                        int index,
                                        TabChangeType change_type) {
  if (webui_hidden_)
    return;
  // TODO(crbug.com/1112496): Support more values for TabChangeType and filter
  // out the changes we are not interested in.
  if (change_type != TabChangeType::kAll)
    return;
  Browser* browser = chrome::FindBrowserWithWebContents(contents);
  if (!browser)
    return;
  TRACE_EVENT0("browser", "custom_metric:TabSearchPageHandler:TabChangedAt");
  page_->TabUpdated(GetTabData(browser->tab_strip_model(), contents, index));
}

void TabSearchPageHandler::ScheduleDebounce() {
  if (!debounce_timer_->IsRunning())
    debounce_timer_->Reset();
}

void TabSearchPageHandler::NotifyTabsChanged() {
  page_->TabsChanged(CreateProfileData());
  debounce_timer_->Stop();
}

bool TabSearchPageHandler::ShouldTrackBrowser(Browser* browser) {
  return browser->profile() == Profile::FromWebUI(web_ui_) &&
         browser->type() == Browser::Type::TYPE_NORMAL;
}

void TabSearchPageHandler::OnVisibilityChanged(content::Visibility visibility) {
  webui_hidden_ = visibility == content::Visibility::HIDDEN;
}

void TabSearchPageHandler::SetTimerForTesting(
    std::unique_ptr<base::RetainingOneShotTimer> timer) {
  debounce_timer_ = std::move(timer);
  debounce_timer_->Start(
      FROM_HERE, kTabsChangeDelay,
      base::BindRepeating(&TabSearchPageHandler::NotifyTabsChanged,
                          base::Unretained(this)));
}
