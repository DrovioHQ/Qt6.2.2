// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuchsia/engine/browser/frame_impl.h"

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <limits>

#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/fuchsia/fuchsia_logging.h"
#include "base/fuchsia/process_context.h"
#include "base/json/json_writer.h"
#include "base/metrics/user_metrics.h"
#include "base/strings/strcat.h"
#include "base/strings/string_piece.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/message_port_provider.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/permission_controller_delegate.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/renderer_preferences_util.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/was_activated_option.mojom.h"
#include "fuchsia/base/mem_buffer_util.h"
#include "fuchsia/base/message_port.h"
#include "fuchsia/engine/browser/accessibility_bridge.h"
#include "fuchsia/engine/browser/cast_streaming_session_client.h"
#include "fuchsia/engine/browser/context_impl.h"
#include "fuchsia/engine/browser/event_filter.h"
#include "fuchsia/engine/browser/frame_layout_manager.h"
#include "fuchsia/engine/browser/frame_window_tree_host.h"
#include "fuchsia/engine/browser/media_player_impl.h"
#include "fuchsia/engine/browser/navigation_policy_handler.h"
#include "fuchsia/engine/browser/web_engine_devtools_controller.h"
#include "fuchsia/engine/common/cast_streaming.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/system/platform_handle.h"
#include "net/base/net_errors.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/common/logging/logging_utils.h"
#include "third_party/blink/public/common/messaging/web_message_port.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom.h"
#include "ui/aura/window.h"
#include "ui/gfx/switches.h"
#include "ui/ozone/public/ozone_switches.h"
#include "ui/wm/core/base_focus_rules.h"
#include "url/gurl.h"

namespace {

// Simulated screen bounds to use when headless rendering is enabled.
constexpr gfx::Size kHeadlessWindowSize = {1, 1};

// Simulated screen bounds to use when testing the SemanticsManager.
constexpr gfx::Size kSemanticsTestingWindowSize = {720, 640};

// A special value which matches all origins when specified in an origin list.
constexpr char kWildcardOrigin[] = "*";

// Used for attaching popup-related metadata to a WebContents.
constexpr char kPopupCreationInfo[] = "popup-creation-info";
class PopupFrameCreationInfoUserData : public base::SupportsUserData::Data {
 public:
  fuchsia::web::PopupFrameCreationInfo info;
};

class FrameFocusRules : public wm::BaseFocusRules {
 public:
  FrameFocusRules() = default;
  ~FrameFocusRules() override = default;

  // wm::BaseFocusRules implementation.
  bool SupportsChildActivation(const aura::Window*) const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(FrameFocusRules);
};

bool FrameFocusRules::SupportsChildActivation(const aura::Window*) const {
  // TODO(crbug.com/878439): Return a result based on window properties such as
  // visibility.
  return true;
}

// TODO(crbug.com/1113289): Use OnLoadScriptInjectorHost's origin matching code.
bool IsUrlMatchedByOriginList(const GURL& url,
                              const std::vector<std::string>& allowed_origins) {
  for (const std::string& origin : allowed_origins) {
    if (origin == kWildcardOrigin)
      return true;

    GURL origin_url(origin);
    if (!origin_url.is_valid()) {
      DLOG(WARNING)
          << "Ignored invalid origin spec when checking allowed list: "
          << origin;
      continue;
    }

    if (origin_url != url.GetOrigin())
      continue;

    return true;
  }
  return false;
}

fx_log_severity_t FuchsiaWebConsoleLogLevelToFxLogSeverity(
    fuchsia::web::ConsoleLogLevel level) {
  switch (level) {
    case fuchsia::web::ConsoleLogLevel::DEBUG:
      return FX_LOG_DEBUG;
    case fuchsia::web::ConsoleLogLevel::INFO:
      return FX_LOG_INFO;
    case fuchsia::web::ConsoleLogLevel::WARN:
      return FX_LOG_WARNING;
    case fuchsia::web::ConsoleLogLevel::ERROR:
      return FX_LOG_ERROR;
    case fuchsia::web::ConsoleLogLevel::NONE:
      return FX_LOG_NONE;
    default:
      // Cope gracefully with callers setting undefined levels.
      DLOG(ERROR) << "Unknown log level:"
                  << static_cast<std::underlying_type<decltype(level)>::type>(
                         level);
      return FX_LOG_NONE;
  }
}

fx_log_severity_t BlinkConsoleMessageLevelToFxLogSeverity(
    blink::mojom::ConsoleMessageLevel level) {
  switch (level) {
    case blink::mojom::ConsoleMessageLevel::kVerbose:
      return FX_LOG_DEBUG;
    case blink::mojom::ConsoleMessageLevel::kInfo:
      return FX_LOG_INFO;
    case blink::mojom::ConsoleMessageLevel::kWarning:
      return FX_LOG_WARNING;
    case blink::mojom::ConsoleMessageLevel::kError:
      return FX_LOG_ERROR;
  }

  // Cope gracefully with callers setting undefined levels.
  DLOG(ERROR) << "Unknown log level:"
              << static_cast<std::underlying_type<decltype(level)>::type>(
                     level);
  return FX_LOG_NONE;
}

bool IsHeadless() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kHeadless);
}

using FrameImplMap =
    base::small_map<std::map<content::WebContents*, FrameImpl*>>;

FrameImplMap& WebContentsToFrameImplMap() {
  static base::NoDestructor<FrameImplMap> frame_impl_map;
  return *frame_impl_map;
}

content::PermissionType FidlPermissionTypeToContentPermissionType(
    fuchsia::web::PermissionType fidl_type) {
  switch (fidl_type) {
    case fuchsia::web::PermissionType::MICROPHONE:
      return content::PermissionType::AUDIO_CAPTURE;
    case fuchsia::web::PermissionType::CAMERA:
      return content::PermissionType::VIDEO_CAPTURE;
    case fuchsia::web::PermissionType::PROTECTED_MEDIA_IDENTIFIER:
      return content::PermissionType::PROTECTED_MEDIA_IDENTIFIER;
    case fuchsia::web::PermissionType::PERSISTENT_STORAGE:
      return content::PermissionType::DURABLE_STORAGE;
  }
}

// Permission request callback for FrameImpl::RequestMediaAccessPermission.
void HandleMediaPermissionsRequestResult(
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback,
    const std::vector<blink::mojom::PermissionStatus>& result) {
  blink::MediaStreamDevices devices;

  int result_pos = 0;

  if (request.audio_type ==
      blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE) {
    if (result[result_pos] == blink::mojom::PermissionStatus::GRANTED) {
      devices.push_back(blink::MediaStreamDevice(
          request.audio_type, request.requested_audio_device_id,
          /*name=*/""));
    }
    result_pos++;
  }

  if (request.video_type ==
      blink::mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE) {
    if (result[result_pos] == blink::mojom::PermissionStatus::GRANTED) {
      devices.push_back(blink::MediaStreamDevice(
          request.video_type, request.requested_video_device_id,
          /*name=*/""));
    }
  }

  std::move(callback).Run(
      devices,
      devices.empty() ? blink::mojom::MediaStreamRequestResult::NO_HARDWARE
                      : blink::mojom::MediaStreamRequestResult::OK,
      nullptr);
}

base::Optional<url::Origin> ParseAndValidateWebOrigin(
    const std::string& origin_str) {
  GURL origin_url(origin_str);
  if (!origin_url.username().empty() || !origin_url.password().empty() ||
      !origin_url.query().empty() || !origin_url.ref().empty()) {
    return base::nullopt;
  }

  if (!origin_url.path().empty() && origin_url.path() != "/")
    return base::nullopt;

  auto origin = url::Origin::Create(origin_url);
  if (origin.opaque())
    return base::nullopt;

  return origin;
}

}  // namespace

// static
FrameImpl* FrameImpl::FromWebContents(content::WebContents* web_contents) {
  if (!web_contents)
    return nullptr;

  auto& map = WebContentsToFrameImplMap();
  auto it = map.find(web_contents);
  DCHECK(it != map.end()) << "WebContents not owned by a FrameImpl.";
  return it->second;
}

// static
FrameImpl* FrameImpl::FromRenderFrameHost(
    content::RenderFrameHost* render_frame_host) {
  return FromWebContents(
      content::WebContents::FromRenderFrameHost(render_frame_host));
}

FrameImpl::FrameImpl(std::unique_ptr<content::WebContents> web_contents,
                     ContextImpl* context,
                     fuchsia::web::CreateFrameParams params,
                     fidl::InterfaceRequest<fuchsia::web::Frame> frame_request)
    : web_contents_(std::move(web_contents)),
      context_(context),
      console_log_tag_(params.has_debug_name() ? params.debug_name()
                                               : std::string()),
      params_for_popups_(std::move(params)),
      navigation_controller_(web_contents_.get()),
      url_request_rewrite_rules_manager_(web_contents_.get()),
      permission_controller_(web_contents_.get()),
      binding_(this, std::move(frame_request)),
      media_blocker_(web_contents_.get()),
      theme_manager_(web_contents_.get()) {
  DCHECK(!WebContentsToFrameImplMap()[web_contents_.get()]);
  WebContentsToFrameImplMap()[web_contents_.get()] = this;

  web_contents_->SetDelegate(this);
  Observe(web_contents_.get());

  binding_.set_error_handler([this](zx_status_t status) {
    ZX_LOG_IF(ERROR, status != ZX_ERR_PEER_CLOSED, status)
        << " Frame disconnected.";
    context_->DestroyFrame(this);
  });

  content::UpdateFontRendererPreferencesFromSystemSettings(
      web_contents_->GetMutableRendererPrefs());
}

FrameImpl::~FrameImpl() {
  DestroyWindowTreeHost();
  context_->devtools_controller()->OnFrameDestroyed(web_contents_.get());

  auto& map = WebContentsToFrameImplMap();
  auto it = WebContentsToFrameImplMap().find(web_contents_.get());
  DCHECK(it != map.end() && it->second == this);
  map.erase(it);
}

zx::unowned_channel FrameImpl::GetBindingChannelForTest() const {
  return zx::unowned_channel(binding_.channel());
}

aura::Window* FrameImpl::root_window() const {
  return window_tree_host_->window();
}

void FrameImpl::ExecuteJavaScriptInternal(std::vector<std::string> origins,
                                          fuchsia::mem::Buffer script,
                                          ExecuteJavaScriptCallback callback,
                                          bool need_result) {
  fuchsia::web::Frame_ExecuteJavaScript_Result result;
  if (!context_->IsJavaScriptInjectionAllowed()) {
    result.set_err(fuchsia::web::FrameError::INTERNAL_ERROR);
    callback(std::move(result));
    return;
  }

  // Prevents script injection into the wrong document if the renderer recently
  // navigated to a different origin.
  if (!IsUrlMatchedByOriginList(web_contents_->GetLastCommittedURL(),
                                origins)) {
    result.set_err(fuchsia::web::FrameError::INVALID_ORIGIN);
    callback(std::move(result));
    return;
  }

  base::string16 script_utf16;
  if (!cr_fuchsia::ReadUTF8FromVMOAsUTF16(script, &script_utf16)) {
    result.set_err(fuchsia::web::FrameError::BUFFER_NOT_UTF8);
    callback(std::move(result));
    return;
  }

  content::RenderFrameHost::JavaScriptResultCallback result_callback;
  if (need_result) {
    result_callback = base::BindOnce(
        [](ExecuteJavaScriptCallback callback, base::Value result_value) {
          fuchsia::web::Frame_ExecuteJavaScript_Result result;

          std::string result_json;
          if (!base::JSONWriter::Write(result_value, &result_json)) {
            result.set_err(fuchsia::web::FrameError::INTERNAL_ERROR);
            callback(std::move(result));
            return;
          }

          fuchsia::web::Frame_ExecuteJavaScript_Response response;
          response.result = cr_fuchsia::MemBufferFromString(
              std::move(result_json), "cr-execute-js-response");
          result.set_response(std::move(response));
          callback(std::move(result));
        },
        std::move(callback));
  }

  web_contents_->GetMainFrame()->ExecuteJavaScript(script_utf16,
                                                   std::move(result_callback));

  if (!need_result) {
    // If no result is required then invoke callback() immediately.
    fuchsia::web::Frame_ExecuteJavaScript_Result result;
    result.set_response(fuchsia::web::Frame_ExecuteJavaScript_Response());
    callback(std::move(result));
  }
}

bool FrameImpl::IsWebContentsCreationOverridden(
    content::SiteInstance* source_site_instance,
    content::mojom::WindowContainerType window_container_type,
    const GURL& opener_url,
    const std::string& frame_name,
    const GURL& target_url) {
  // Specify a generous upper bound for unacknowledged popup windows, so that we
  // can catch bad client behavior while not interfering with normal operation.
  constexpr size_t kMaxPendingWebContentsCount = 10;

  if (!popup_listener_)
    return true;

  if (pending_popups_.size() >= kMaxPendingWebContentsCount) {
    // The content is producing popups faster than the embedder can process
    // them. Drop the popups so as to prevent resource exhaustion.
    LOG(WARNING) << "Too many pending popups, ignoring request.";

    // Don't produce a WebContents for this popup.
    return true;
  }

  return false;
}

void FrameImpl::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const gfx::Rect& initial_rect,
    bool user_gesture,
    bool* was_blocked) {
  DCHECK_EQ(source, web_contents_.get());

  // TODO(crbug.com/995395): Add window disposition to the FIDL interface.
  switch (disposition) {
    case WindowOpenDisposition::NEW_FOREGROUND_TAB:
    case WindowOpenDisposition::NEW_BACKGROUND_TAB:
    case WindowOpenDisposition::NEW_POPUP:
    case WindowOpenDisposition::NEW_WINDOW: {
      if (url_request_rewrite_rules_manager_.GetCachedRules()) {
        // There is no support for URL request rules rewriting with popups.
        *was_blocked = true;
        return;
      }

      PopupFrameCreationInfoUserData* popup_creation_info =
          reinterpret_cast<PopupFrameCreationInfoUserData*>(
              new_contents->GetUserData(kPopupCreationInfo));
      popup_creation_info->info.set_initiated_by_user(user_gesture);
      pending_popups_.emplace_back(std::move(new_contents));
      MaybeSendPopup();
      return;
    }

    // These kinds of windows don't produce Frames.
    case WindowOpenDisposition::CURRENT_TAB:
    case WindowOpenDisposition::SINGLETON_TAB:
    case WindowOpenDisposition::SAVE_TO_DISK:
    case WindowOpenDisposition::OFF_THE_RECORD:
    case WindowOpenDisposition::IGNORE_ACTION:
    case WindowOpenDisposition::SWITCH_TO_TAB:
    case WindowOpenDisposition::UNKNOWN:
      NOTIMPLEMENTED() << "Dropped new web contents (disposition: "
                       << static_cast<int>(disposition) << ")";
      return;
  }
}

void FrameImpl::WebContentsCreated(content::WebContents* source_contents,
                                   int opener_render_process_id,
                                   int opener_render_frame_id,
                                   const std::string& frame_name,
                                   const GURL& target_url,
                                   content::WebContents* new_contents) {
  auto creation_info = std::make_unique<PopupFrameCreationInfoUserData>();
  creation_info->info.set_initial_url(target_url.spec());
  new_contents->SetUserData(kPopupCreationInfo, std::move(creation_info));
}

void FrameImpl::MaybeSendPopup() {
  if (!popup_listener_)
    return;

  if (popup_ack_outstanding_ || pending_popups_.empty())
    return;

  std::unique_ptr<content::WebContents> popup =
      std::move(pending_popups_.front());
  pending_popups_.pop_front();

  fuchsia::web::PopupFrameCreationInfo creation_info =
      std::move(reinterpret_cast<PopupFrameCreationInfoUserData*>(
                    popup->GetUserData(kPopupCreationInfo))
                    ->info);

  // The PopupFrameCreationInfo won't be needed anymore, so clear it out.
  popup->SetUserData(kPopupCreationInfo, nullptr);

  // ContextImpl::CreateFrameInternal() verified that |params_for_popups_| can
  // be cloned, so it cannot fail here.
  fuchsia::web::CreateFrameParams params;
  CHECK_EQ(ZX_OK, params_for_popups_.Clone(&params));

  fidl::InterfaceHandle<fuchsia::web::Frame> frame_handle;
  context_->CreateFrameForWebContents(std::move(popup), std::move(params),
                                      frame_handle.NewRequest());
  popup_listener_->OnPopupFrameCreated(std::move(frame_handle),
                                       std::move(creation_info), [this] {
                                         popup_ack_outstanding_ = false;
                                         MaybeSendPopup();
                                       });
  popup_ack_outstanding_ = true;
}

void FrameImpl::DestroyWindowTreeHost() {
  if (!window_tree_host_)
    return;

  aura::client::SetFocusClient(root_window(), nullptr);
  wm::SetActivationClient(root_window(), nullptr);
  root_window()->RemovePreTargetHandler(&event_filter_);
  root_window()->RemovePreTargetHandler(focus_controller_.get());
  web_contents_->GetNativeView()->Hide();
  window_tree_host_->Hide();
  window_tree_host_->compositor()->SetVisible(false);
  window_tree_host_.reset();
  accessibility_bridge_.reset();

  // Allows posted focus events to process before the FocusController is torn
  // down.
  content::GetUIThreadTaskRunner({})->DeleteSoon(FROM_HERE,
                                                 std::move(focus_controller_));
}

void FrameImpl::CloseAndDestroyFrame(zx_status_t error) {
  DCHECK(binding_.is_bound());
  binding_.Close(error);
  context_->DestroyFrame(this);
}

void FrameImpl::OnPopupListenerDisconnected(zx_status_t status) {
  ZX_LOG_IF(WARNING, status != ZX_ERR_PEER_CLOSED, status)
      << "Popup listener disconnected.";
  pending_popups_.clear();
}

void FrameImpl::OnMediaPlayerDisconnect() {
  media_player_ = nullptr;
}

void FrameImpl::OnAccessibilityError(zx_status_t error) {
  // The task is posted so |accessibility_bridge_| does not tear |this| down
  // while events are still being processed.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&FrameImpl::CloseAndDestroyFrame,
                                weak_factory_.GetWeakPtr(), error));
}

bool FrameImpl::MaybeHandleCastStreamingMessage(
    std::string* origin,
    fuchsia::web::WebMessage* message,
    PostMessageCallback* callback) {
  if (!IsCastStreamingEnabled())
    return false;

  if (!IsCastStreamingAppOrigin(*origin))
    return false;

  fuchsia::web::Frame_PostMessage_Result result;
  if (cast_streaming_session_client_ ||
      !IsValidCastStreamingMessage(*message)) {
    // The Cast Streaming MessagePort should only be set once and |message|
    // should be a valid Cast Streaming Message.
    result.set_err(fuchsia::web::FrameError::INVALID_ORIGIN);
    (*callback)(std::move(result));
    return true;
  }

  cast_streaming_session_client_ = std::make_unique<CastStreamingSessionClient>(
      std::move((*message->mutable_outgoing_transfer())[0].message_port()));
  result.set_response(fuchsia::web::Frame_PostMessage_Response());
  (*callback)(std::move(result));
  return true;
}

void FrameImpl::MaybeStartCastStreaming(
    content::NavigationHandle* navigation_handle) {
  if (!IsCastStreamingEnabled() || !cast_streaming_session_client_)
    return;

  mojo::AssociatedRemote<mojom::CastStreamingReceiver> cast_streaming_receiver;
  navigation_handle->GetRenderFrameHost()
      ->GetRemoteAssociatedInterfaces()
      ->GetInterface(&cast_streaming_receiver);
  cast_streaming_session_client_->StartMojoConnection(
      std::move(cast_streaming_receiver));
}

void FrameImpl::CreateView(fuchsia::ui::views::ViewToken view_token) {
  scenic::ViewRefPair view_ref_pair = scenic::ViewRefPair::New();
  CreateViewWithViewRef(std::move(view_token),
                        std::move(view_ref_pair.control_ref),
                        std::move(view_ref_pair.view_ref));
}

void FrameImpl::CreateViewWithViewRef(
    fuchsia::ui::views::ViewToken view_token,
    fuchsia::ui::views::ViewRefControl control_ref,
    fuchsia::ui::views::ViewRef view_ref) {
  if (IsHeadless()) {
    LOG(WARNING) << "CreateView() called on a HEADLESS Context.";
    CloseAndDestroyFrame(ZX_ERR_INVALID_ARGS);
    return;
  }

  // If a View to this Frame is already active then disconnect it.
  DestroyWindowTreeHost();

  scenic::ViewRefPair view_ref_pair;
  view_ref_pair.control_ref = std::move(control_ref);
  view_ref_pair.view_ref = std::move(view_ref);
  InitWindowTreeHost(std::move(view_token), std::move(view_ref_pair));

  fuchsia::accessibility::semantics::SemanticsManagerPtr semantics_manager;
  if (!semantics_manager_for_test_) {
    semantics_manager =
        base::ComponentContextForProcess()
            ->svc()
            ->Connect<fuchsia::accessibility::semantics::SemanticsManager>();
  }

  // If the SemanticTree owned by |accessibility_bridge_| is disconnected, it
  // will cause |this| to be closed.
  accessibility_bridge_ = std::make_unique<AccessibilityBridge>(
      semantics_manager_for_test_ ? semantics_manager_for_test_
                                  : semantics_manager.get(),
      window_tree_host_->CreateViewRef(), web_contents_.get(),
      base::BindOnce(&FrameImpl::OnAccessibilityError, base::Unretained(this)));
}

void FrameImpl::GetMediaPlayer(
    fidl::InterfaceRequest<fuchsia::media::sessions2::Player> player) {
  media_player_ = std::make_unique<MediaPlayerImpl>(
      content::MediaSession::Get(web_contents_.get()), std::move(player),
      base::BindOnce(&FrameImpl::OnMediaPlayerDisconnect,
                     base::Unretained(this)));
}

void FrameImpl::GetNavigationController(
    fidl::InterfaceRequest<fuchsia::web::NavigationController> controller) {
  navigation_controller_.AddBinding(std::move(controller));
}

void FrameImpl::ExecuteJavaScript(std::vector<std::string> origins,
                                  fuchsia::mem::Buffer script,
                                  ExecuteJavaScriptCallback callback) {
  ExecuteJavaScriptInternal(std::move(origins), std::move(script),
                            std::move(callback), true);
}

void FrameImpl::ExecuteJavaScriptNoResult(
    std::vector<std::string> origins,
    fuchsia::mem::Buffer script,
    ExecuteJavaScriptNoResultCallback callback) {
  ExecuteJavaScriptInternal(
      std::move(origins), std::move(script),
      [callback = std::move(callback)](
          fuchsia::web::Frame_ExecuteJavaScript_Result result_with_value) {
        fuchsia::web::Frame_ExecuteJavaScriptNoResult_Result result;
        if (result_with_value.is_err()) {
          result.set_err(result_with_value.err());
        } else {
          result.set_response(
              fuchsia::web::Frame_ExecuteJavaScriptNoResult_Response());
        }
        callback(std::move(result));
      },
      false);
}

void FrameImpl::AddBeforeLoadJavaScript(
    uint64_t id,
    std::vector<std::string> origins,
    fuchsia::mem::Buffer script,
    AddBeforeLoadJavaScriptCallback callback) {
  constexpr char kWildcardOrigin[] = "*";

  fuchsia::web::Frame_AddBeforeLoadJavaScript_Result result;
  if (!context_->IsJavaScriptInjectionAllowed()) {
    result.set_err(fuchsia::web::FrameError::INTERNAL_ERROR);
    callback(std::move(result));
    return;
  }

  std::string script_as_string;
  if (!cr_fuchsia::StringFromMemBuffer(script, &script_as_string)) {
    LOG(ERROR) << "Couldn't read script from buffer.";
    result.set_err(fuchsia::web::FrameError::INTERNAL_ERROR);
    callback(std::move(result));
    return;
  }

  // TODO(crbug.com/1108607): Only allow wildcards to be specified standalone.
  if (std::any_of(origins.begin(), origins.end(),
                  [kWildcardOrigin](base::StringPiece origin) {
                    return origin == kWildcardOrigin;
                  })) {
    script_injector_.AddScriptForAllOrigins(id, script_as_string);
  } else {
    std::vector<url::Origin> origins_converted;
    for (const std::string& origin : origins) {
      url::Origin origin_parsed = url::Origin::Create(GURL(origin));
      if (origin_parsed.opaque()) {
        result.set_err(fuchsia::web::FrameError::INVALID_ORIGIN);
        callback(std::move(result));
        return;
      }
      origins_converted.push_back(origin_parsed);
    }

    script_injector_.AddScript(id, origins_converted, script_as_string);
  }

  result.set_response(fuchsia::web::Frame_AddBeforeLoadJavaScript_Response());
  callback(std::move(result));
}

void FrameImpl::RemoveBeforeLoadJavaScript(uint64_t id) {
  script_injector_.RemoveScript(id);
}

void FrameImpl::PostMessage(std::string origin,
                            fuchsia::web::WebMessage message,
                            PostMessageCallback callback) {
  if (MaybeHandleCastStreamingMessage(&origin, &message, &callback))
    return;

  fuchsia::web::Frame_PostMessage_Result result;
  if (origin.empty()) {
    result.set_err(fuchsia::web::FrameError::INVALID_ORIGIN);
    callback(std::move(result));
    return;
  }

  if (!message.has_data()) {
    result.set_err(fuchsia::web::FrameError::NO_DATA_IN_MESSAGE);
    callback(std::move(result));
    return;
  }

  base::Optional<base::string16> origin_utf16;
  if (origin != kWildcardOrigin)
    origin_utf16 = base::UTF8ToUTF16(origin);

  base::string16 data_utf16;
  if (!cr_fuchsia::ReadUTF8FromVMOAsUTF16(message.data(), &data_utf16)) {
    result.set_err(fuchsia::web::FrameError::BUFFER_NOT_UTF8);
    callback(std::move(result));
    return;
  }

  // Convert and pass along any MessagePorts contained in the message.
  std::vector<blink::WebMessagePort> message_ports;
  if (message.has_outgoing_transfer()) {
    for (const fuchsia::web::OutgoingTransferable& outgoing :
         message.outgoing_transfer()) {
      if (!outgoing.is_message_port()) {
        result.set_err(fuchsia::web::FrameError::INTERNAL_ERROR);
        callback(std::move(result));
        return;
      }
    }

    for (fuchsia::web::OutgoingTransferable& outgoing :
         *message.mutable_outgoing_transfer()) {
      blink::WebMessagePort blink_port = cr_fuchsia::BlinkMessagePortFromFidl(
          std::move(outgoing.message_port()));
      if (!blink_port.IsValid()) {
        result.set_err(fuchsia::web::FrameError::INTERNAL_ERROR);
        callback(std::move(result));
        return;
      }
      message_ports.push_back(std::move(blink_port));
    }
  }

  content::MessagePortProvider::PostMessageToFrame(
      web_contents_.get(), base::string16(), origin_utf16,
      std::move(data_utf16), std::move(message_ports));
  result.set_response(fuchsia::web::Frame_PostMessage_Response());
  callback(std::move(result));
}

void FrameImpl::SetNavigationEventListener(
    fidl::InterfaceHandle<fuchsia::web::NavigationEventListener> listener) {
  navigation_controller_.SetEventListener(std::move(listener));
}

void FrameImpl::SetJavaScriptLogLevel(fuchsia::web::ConsoleLogLevel level) {
  log_level_ = FuchsiaWebConsoleLogLevelToFxLogSeverity(level);
}

void FrameImpl::SetConsoleLogSink(fuchsia::logger::LogSinkHandle sink) {
  if (sink) {
    console_logger_ = base::CreateFxLoggerFromLogSinkWithTag(std::move(sink),
                                                             console_log_tag_);
  } else {
    console_logger_ = nullptr;
  }
}

void FrameImpl::ConfigureInputTypes(fuchsia::web::InputTypes types,
                                    fuchsia::web::AllowInputState allow) {
  event_filter_.ConfigureInputTypes(types, allow);
}

void FrameImpl::SetPopupFrameCreationListener(
    fidl::InterfaceHandle<fuchsia::web::PopupFrameCreationListener> listener) {
  popup_listener_ = listener.Bind();
  popup_listener_.set_error_handler(
      fit::bind_member(this, &FrameImpl::OnPopupListenerDisconnected));
}

void FrameImpl::SetUrlRequestRewriteRules(
    std::vector<fuchsia::web::UrlRequestRewriteRule> rules,
    SetUrlRequestRewriteRulesCallback callback) {
  zx_status_t error = url_request_rewrite_rules_manager_.OnRulesUpdated(
      std::move(rules), std::move(callback));
  if (error != ZX_OK) {
    CloseAndDestroyFrame(error);
    return;
  }
}

void FrameImpl::EnableHeadlessRendering() {
  if (!IsHeadless()) {
    LOG(ERROR) << "EnableHeadlessRendering() on non-HEADLESS Context.";
    CloseAndDestroyFrame(ZX_ERR_INVALID_ARGS);
    return;
  }

  scenic::ViewRefPair view_ref_pair = scenic::ViewRefPair::New();
  InitWindowTreeHost(fuchsia::ui::views::ViewToken(), std::move(view_ref_pair));

  gfx::Rect bounds(kHeadlessWindowSize);
  if (semantics_manager_for_test_) {
    accessibility_bridge_ = std::make_unique<AccessibilityBridge>(
        semantics_manager_for_test_, window_tree_host_->CreateViewRef(),
        web_contents_.get(),
        base::BindOnce(&FrameImpl::OnAccessibilityError,
                       base::Unretained(this)));

    // Set bounds for testing hit testing.
    bounds.set_size(kSemanticsTestingWindowSize);
  }

  window_tree_host_->SetBoundsInPixels(bounds);

  // FrameWindowTreeHost will Show() itself when the View is attached, but
  // in headless mode there is no View, so Show() it explicitly.
  window_tree_host_->Show();
}

void FrameImpl::DisableHeadlessRendering() {
  if (!IsHeadless()) {
    LOG(ERROR)
        << "Attempted to disable headless rendering on non-HEADLESS Context.";
    CloseAndDestroyFrame(ZX_ERR_INVALID_ARGS);
    return;
  }

  DestroyWindowTreeHost();
}

void FrameImpl::InitWindowTreeHost(fuchsia::ui::views::ViewToken view_token,
                                   scenic::ViewRefPair view_ref_pair) {
  DCHECK(!window_tree_host_);

  window_tree_host_ = std::make_unique<FrameWindowTreeHost>(
      std::move(view_token), std::move(view_ref_pair), web_contents_.get());
  window_tree_host_->InitHost();
  root_window()->AddPreTargetHandler(&event_filter_);

  // Add hooks which automatically set the focus state when input events are
  // received.
  focus_controller_ =
      std::make_unique<wm::FocusController>(new FrameFocusRules);
  root_window()->AddPreTargetHandler(focus_controller_.get());
  aura::client::SetFocusClient(root_window(), focus_controller_.get());

  wm::SetActivationClient(root_window(), focus_controller_.get());

  layout_manager_ = new FrameLayoutManager;
  root_window()->SetLayoutManager(layout_manager_);  // Transfers ownership.
  if (!render_size_override_.IsEmpty())
    layout_manager_->ForceContentDimensions(render_size_override_);

  root_window()->AddChild(web_contents_->GetNativeView());
  web_contents_->GetNativeView()->Show();

  // FrameWindowTreeHost will Show() itself when the View is actually attached
  // to the view-tree to be displayed. See https://crbug.com/1109270
}

void FrameImpl::SetMediaSessionId(uint64_t session_id) {
  media_session_id_ = session_id;
}

void FrameImpl::MediaStartedPlaying(const MediaPlayerInfo& video_type,
                                    const content::MediaPlayerId& id) {
  base::RecordComputedAction("MediaPlay");
}

void FrameImpl::MediaStoppedPlaying(
    const MediaPlayerInfo& video_type,
    const content::MediaPlayerId& id,
    WebContentsObserver::MediaStoppedReason reason) {
  base::RecordComputedAction("MediaPause");
}

void FrameImpl::GetPrivateMemorySize(GetPrivateMemorySizeCallback callback) {
  if (!web_contents_->GetMainFrame()->GetProcess()->IsReady()) {
    // Renderer process is not yet started.
    callback(0);
    return;
  }

  zx_info_task_stats_t task_stats;
  zx_status_t status = zx_object_get_info(
      web_contents_->GetMainFrame()->GetProcess()->GetProcess().Handle(),
      ZX_INFO_TASK_STATS, &task_stats, sizeof(task_stats), nullptr, nullptr);

  if (status != ZX_OK) {
    // Fail gracefully by returning zero.
    ZX_LOG(WARNING, status) << "zx_object_get_info(ZX_INFO_TASK_STATS)";
    callback(0);
    return;
  }

  callback(task_stats.mem_private_bytes);
}

void FrameImpl::SetNavigationPolicyProvider(
    fuchsia::web::NavigationPolicyProviderParams params,
    fidl::InterfaceHandle<fuchsia::web::NavigationPolicyProvider> provider) {
  navigation_policy_handler_ = std::make_unique<NavigationPolicyHandler>(
      std::move(params), std::move(provider));
}

void FrameImpl::SetPreferredTheme(fuchsia::settings::ThemeType theme) {
  theme_manager_.SetTheme(theme, base::BindOnce(
                                     [](FrameImpl* frame_impl, bool result) {
                                       // TODO(crbug.com/1148454): Destroy the
                                       // frame once a fake Display service is
                                       // implemented.

                                       // if (!result)
                                       //   frame_impl->CloseAndDestroyFrame
                                       //       ZX_ERR_INVALID_ARGS);
                                     },
                                     base::Unretained(this)));
}

void FrameImpl::ForceContentDimensions(
    std::unique_ptr<fuchsia::ui::gfx::vec2> web_dips) {
  if (!web_dips) {
    render_size_override_ = {};
    if (layout_manager_)
      layout_manager_->ForceContentDimensions({});
    return;
  }

  gfx::Size web_dips_converted(web_dips->x, web_dips->y);
  if (web_dips_converted.IsEmpty()) {
    LOG(WARNING) << "Rejecting zero-area size for ForceContentDimensions().";
    CloseAndDestroyFrame(ZX_ERR_INVALID_ARGS);
    return;
  }

  render_size_override_ = web_dips_converted;
  if (layout_manager_)
    layout_manager_->ForceContentDimensions(web_dips_converted);
}

void FrameImpl::SetPermissionState(
    fuchsia::web::PermissionDescriptor fidl_permission,
    std::string web_origin_string,
    fuchsia::web::PermissionState fidl_state) {
  if (!fidl_permission.has_type()) {
    LOG(ERROR) << "PermissionDescriptor.type is not specified in "
                  "SetPermissionState().";
    CloseAndDestroyFrame(ZX_ERR_INVALID_ARGS);
    return;
  }

  content::PermissionType type =
      FidlPermissionTypeToContentPermissionType(fidl_permission.type());

  blink::mojom::PermissionStatus state =
      (fidl_state == fuchsia::web::PermissionState::GRANTED)
          ? blink::mojom::PermissionStatus::GRANTED
          : blink::mojom::PermissionStatus::DENIED;

  // TODO(crbug.com/1136994): Remove this once the PermissionManager API is
  // available.
  if (web_origin_string == "*" &&
      type == content::PermissionType::PROTECTED_MEDIA_IDENTIFIER) {
    permission_controller_.SetDefaultPermissionState(type, state);
    return;
  }

  // Handle per-origin permissions specifications.
  auto web_origin = ParseAndValidateWebOrigin(web_origin_string);
  if (!web_origin) {
    LOG(ERROR) << "SetPermissionState() called with invalid web_origin: "
               << web_origin_string;
    CloseAndDestroyFrame(ZX_ERR_INVALID_ARGS);
    return;
  }

  permission_controller_.SetPermissionState(type, web_origin.value(), state);
}

void FrameImpl::CloseContents(content::WebContents* source) {
  DCHECK_EQ(source, web_contents_.get());
  CloseAndDestroyFrame(ZX_OK);
}

void FrameImpl::SetBlockMediaLoading(bool blocked) {
  media_blocker_.BlockMediaLoading(blocked);
}

bool FrameImpl::DidAddMessageToConsole(
    content::WebContents* source,
    blink::mojom::ConsoleMessageLevel log_level,
    const base::string16& message,
    int32_t line_no,
    const base::string16& source_id) {
  fx_log_severity_t severity =
      BlinkConsoleMessageLevelToFxLogSeverity(log_level);
  if (severity < log_level_) {
    // Prevent the default logging mechanism from logging the message.
    return true;
  }

  if (!console_logger_) {
    // Log via the process' LogSink service if none was set on the Frame.
    // Connect on-demand, so that embedders need not provide a LogSink in the
    // CreateContextParams services, unless they actually enable logging.
    console_logger_ = base::CreateFxLoggerFromLogSinkWithTag(
        base::ComponentContextForProcess()
            ->svc()
            ->Connect<fuchsia::logger::LogSink>(),
        console_log_tag_);
  }

  std::string formatted_message =
      base::StringPrintf("%s:%d : %s", base::UTF16ToUTF8(source_id).data(),
                         line_no, base::UTF16ToUTF8(message).data());
  fx_logger_log(console_logger_.get(), severity, nullptr,
                formatted_message.data());
  return true;
}

void FrameImpl::RequestMediaAccessPermission(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback) {
  DCHECK_EQ(web_contents_.get(), web_contents);

  std::vector<content::PermissionType> permissions;

  if (request.audio_type ==
      blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE) {
    permissions.push_back(content::PermissionType::AUDIO_CAPTURE);
  } else if (request.audio_type != blink::mojom::MediaStreamType::NO_SERVICE) {
    std::move(callback).Run(
        blink::MediaStreamDevices(),
        blink::mojom::MediaStreamRequestResult::NOT_SUPPORTED, nullptr);
    return;
  }

  if (request.video_type ==
      blink::mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE) {
    permissions.push_back(content::PermissionType::VIDEO_CAPTURE);
  } else if (request.video_type != blink::mojom::MediaStreamType::NO_SERVICE) {
    std::move(callback).Run(
        blink::MediaStreamDevices(),
        blink::mojom::MediaStreamRequestResult::NOT_SUPPORTED, nullptr);
    return;
  }

  auto* render_frame_host = content::RenderFrameHost::FromID(
      request.render_process_id, request.render_frame_id);
  if (!render_frame_host) {
    std::move(callback).Run(
        blink::MediaStreamDevices(),
        blink::mojom::MediaStreamRequestResult::INVALID_STATE, nullptr);
    return;
  }

  auto* permission_controller =
      web_contents_->GetBrowserContext()->GetPermissionControllerDelegate();
  permission_controller->RequestPermissions(
      permissions, render_frame_host, request.security_origin,
      request.user_gesture,
      base::BindOnce(&HandleMediaPermissionsRequestResult, request,
                     std::move(callback)));
}

bool FrameImpl::CheckMediaAccessPermission(
    content::RenderFrameHost* render_frame_host,
    const GURL& security_origin,
    blink::mojom::MediaStreamType type) {
  content::PermissionType permission;
  switch (type) {
    case blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE:
      permission = content::PermissionType::AUDIO_CAPTURE;
      break;
    case blink::mojom::MediaStreamType::DEVICE_VIDEO_CAPTURE:
      permission = content::PermissionType::VIDEO_CAPTURE;
      break;
    default:
      NOTREACHED();
      return false;
  }
  auto* permission_controller =
      web_contents_->GetBrowserContext()->GetPermissionControllerDelegate();
  return permission_controller->GetPermissionStatusForFrame(
             permission, render_frame_host, security_origin) ==
         blink::mojom::PermissionStatus::GRANTED;
}

bool FrameImpl::CanOverscrollContent() {
  // Don't process "overscroll" events (e.g. pull-to-refresh, swipe back,
  // swipe forward).
  // TODO(crbug/1177399): Add overscroll toggle to Frame API.
  return false;
}

void FrameImpl::ReadyToCommitNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInMainFrame() ||
      navigation_handle->IsSameDocument() || navigation_handle->IsErrorPage()) {
    return;
  }

  script_injector_.InjectScriptsForURL(navigation_handle->GetURL(),
                                       navigation_handle->GetRenderFrameHost());

  MaybeStartCastStreaming(navigation_handle);
}

void FrameImpl::DidFinishLoad(content::RenderFrameHost* render_frame_host,
                              const GURL& validated_url) {
  context_->devtools_controller()->OnFrameLoaded(web_contents_.get());
}

void FrameImpl::RenderFrameCreated(content::RenderFrameHost* frame_host) {
  // The top-level frame is given a transparent background color.
  if (frame_host == web_contents()->GetMainFrame())
    frame_host->GetView()->SetBackgroundColor(SK_AlphaTRANSPARENT);
}

void FrameImpl::DidFirstVisuallyNonEmptyPaint() {
  base::RecordComputedAction("AppFirstPaint");
}

void FrameImpl::ResourceLoadComplete(
    content::RenderFrameHost* render_frame_host,
    const content::GlobalRequestID& request_id,
    const blink::mojom::ResourceLoadInfo& resource_load_info) {
  int net_error = resource_load_info.net_error;
  if (net_error != net::OK) {
    base::RecordComputedAction(
        base::StringPrintf("WebEngine.ResourceRequestError:%d", net_error));
  }
}

// TODO(crbug.com/1136681#c6): Move below GetBindingChannelForTest when fixed.
void FrameImpl::EnableExplicitSitesFilter(std::string error_page) {
  explicit_sites_filter_error_page_ = std::move(error_page);
}
