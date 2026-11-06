import { NextResponse } from "next/server";

import { NativeError } from "./native";
import { ApiError } from "./params";

export function jsonOk<T>(data: T, init?: ResponseInit) {
  return NextResponse.json(data, init);
}

export function jsonError(error: unknown) {
  if (error instanceof ApiError) {
    return NextResponse.json({ error: error.message, kind: "validation" }, { status: error.status });
  }
  if (error instanceof NativeError) {
    return NextResponse.json(
      { error: error.message, kind: "native", detail: error.detail },
      { status: 502 },
    );
  }
  const message = error instanceof Error ? error.message : "unexpected server error";
  return NextResponse.json({ error: message, kind: "server" }, { status: 500 });
}

export async function readJson(request: Request): Promise<Record<string, unknown>> {
  const text = await request.text();
  if (!text.trim()) return {};
  try {
    const parsed: unknown = JSON.parse(text);
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
      throw new ApiError(400, "request body must be a JSON object");
    }
    return parsed as Record<string, unknown>;
  } catch (error) {
    if (error instanceof ApiError) throw error;
    throw new ApiError(400, "request body is not valid JSON");
  }
}

export function parseId(value: string): number {
  const id = Number.parseInt(value, 10);
  if (!Number.isFinite(id) || id <= 0) throw new ApiError(400, "id must be a positive integer");
  return id;
}
