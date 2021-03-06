// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DEVTOOLS_SERVICE_WORKER_DEVTOOLS_AGENT_HOST_H_
#define CONTENT_BROWSER_DEVTOOLS_SERVICE_WORKER_DEVTOOLS_AGENT_HOST_H_

#include <stdint.h>

#include <map>

#include "base/macros.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "content/browser/devtools/devtools_agent_host_impl.h"
#include "content/browser/devtools/service_worker_devtools_manager.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_process_host_observer.h"
#include "services/network/public/cpp/cross_origin_embedder_policy.h"
#include "services/network/public/mojom/cross_origin_embedder_policy.mojom.h"
#include "third_party/blink/public/mojom/devtools/devtools_agent.mojom.h"

namespace content {

class BrowserContext;

class ServiceWorkerDevToolsAgentHost : public DevToolsAgentHostImpl,
                                       RenderProcessHostObserver {
 public:
  using List = std::vector<scoped_refptr<ServiceWorkerDevToolsAgentHost>>;
  using Map = std::map<std::string,
                       scoped_refptr<ServiceWorkerDevToolsAgentHost>>;

  ServiceWorkerDevToolsAgentHost(
      int worker_process_id,
      int worker_route_id,
      scoped_refptr<ServiceWorkerContextWrapper> context_wrapper,
      int64_t version_id,
      const GURL& url,
      const GURL& scope,
      bool is_installed_version,
      base::Optional<network::CrossOriginEmbedderPolicy>
          cross_origin_embedder_policy,
      mojo::PendingRemote<network::mojom::CrossOriginEmbedderPolicyReporter>
          coep_reporter,
      const base::UnguessableToken& devtools_worker_token);

  // DevToolsAgentHost overrides.
  BrowserContext* GetBrowserContext() override;
  std::string GetType() override;
  std::string GetTitle() override;
  GURL GetURL() override;
  bool Activate() override;
  void Reload() override;
  bool Close() override;
  NetworkLoaderFactoryParamsAndInfo CreateNetworkFactoryParamsForDevTools()
      override;
  RenderProcessHost* GetProcessHost() override;
  base::Optional<network::CrossOriginEmbedderPolicy>
  cross_origin_embedder_policy(const std::string& id) override;

  void WorkerRestarted(int worker_process_id, int worker_route_id);
  void WorkerReadyForInspection(
      mojo::PendingRemote<blink::mojom::DevToolsAgent> agent_remote,
      mojo::PendingReceiver<blink::mojom::DevToolsAgentHost> host_receiver);
  void UpdateCrossOriginEmbedderPolicy(
      network::CrossOriginEmbedderPolicy cross_origin_embedder_policy,
      mojo::PendingRemote<network::mojom::CrossOriginEmbedderPolicyReporter>
          coep_reporter);
  void WorkerStopped();
  void WorkerVersionInstalled();
  void WorkerVersionDoomed();

  const GURL& scope() const { return scope_; }
  const base::UnguessableToken& devtools_worker_token() const {
    return devtools_worker_token_;
  }

  // If the ServiceWorker has been installed before the worker instance started,
  // it returns the time when the instance started. Otherwise returns the time
  // when the ServiceWorker was installed.
  base::Time version_installed_time() const { return version_installed_time_; }

  // Returns the time when the ServiceWorker was doomed.
  base::Time version_doomed_time() const { return version_doomed_time_; }

  int64_t version_id() const { return version_id_; }
  const ServiceWorkerContextWrapper* context_wrapper() const {
    return context_wrapper_.get();
  }

 private:
  ~ServiceWorkerDevToolsAgentHost() override;
  void UpdateIsAttached(bool attached);
  void UpdateProcessHost();

  // DevToolsAgentHostImpl overrides.
  bool AttachSession(DevToolsSession* session, bool acquire_wake_lock) override;
  void DetachSession(DevToolsSession* session) override;

  // RenderProcessHostObserver implementation.
  void RenderProcessHostDestroyed(RenderProcessHost* host) override;

  void UpdateLoaderFactories(base::OnceClosure callback);

  enum WorkerState {
    WORKER_NOT_READY,
    WORKER_READY,
    WORKER_TERMINATED,
  };
  WorkerState state_;
  base::UnguessableToken devtools_worker_token_;
  int worker_process_id_;
  int worker_route_id_;
  scoped_refptr<ServiceWorkerContextWrapper> context_wrapper_;
  int64_t version_id_;
  GURL url_;
  GURL scope_;
  base::Time version_installed_time_;
  base::Time version_doomed_time_;
  base::Optional<network::CrossOriginEmbedderPolicy>
      cross_origin_embedder_policy_;
  mojo::Remote<network::mojom::CrossOriginEmbedderPolicyReporter>
      coep_reporter_;
  base::ScopedObservation<RenderProcessHost, RenderProcessHostObserver>
      process_observation_{this};

  DISALLOW_COPY_AND_ASSIGN(ServiceWorkerDevToolsAgentHost);
};

}  // namespace content

#endif  // CONTENT_BROWSER_DEVTOOLS_SERVICE_WORKER_DEVTOOLS_AGENT_HOST_H_
