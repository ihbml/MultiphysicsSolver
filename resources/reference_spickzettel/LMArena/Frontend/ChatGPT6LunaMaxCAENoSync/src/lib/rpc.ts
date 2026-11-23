import { referenceUiSchema } from "@/lib/schema";

export type RpcRequest = {
  jsonrpc: "2.0";
  id: number;
  method: string;
  params: Record<string, unknown>;
};

export type RpcLogEntry = {
  id: number;
  method: string;
  message: string;
  time: string;
  kind: "request" | "stream" | "warning";
};

export const rpcEndpoints = [
  "system.hello",
  "schema.get",
  "geometry.import",
  "geometry.patches.update",
  "mesh.generate",
  "job.cancel",
  "materials.list",
  "case.validate",
  "case.generate",
  "solver.start",
  "solver.write_now",
  "solver.stop_graceful",
  "solver.abort",
  "results.list_times",
  "results.list_fields",
  "results.load",
] as const;

export class DemoRpcClient {
  private sequence = 1;
  private listener?: (entry: RpcLogEntry) => void;

  setLogListener(listener: (entry: RpcLogEntry) => void) {
    this.listener = listener;
  }

  async request(method: string, params: Record<string, unknown> = {}) {
    const request: RpcRequest = {
      jsonrpc: "2.0",
      id: this.sequence++,
      method,
      params,
    };
    const time = new Date().toLocaleTimeString("en-US", { hour12: false });
    this.listener?.({ id: request.id, method, message: "request queued", time, kind: "request" });

    // The preview uses this adapter until a native stdio bridge is attached.
    await new Promise((resolve) => window.setTimeout(resolve, 180));
    const response = {
      jsonrpc: "2.0" as const,
      id: request.id,
      result: method === "schema.get" ? referenceUiSchema : { accepted: true, method, params },
    };
    this.listener?.({
      id: request.id,
      method,
      message: "demo transport acknowledged",
      time: new Date().toLocaleTimeString("en-US", { hour12: false }),
      kind: "stream",
    });
    return response;
  }
}