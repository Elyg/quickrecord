import { closeMainWindow, getPreferenceValues, showHUD } from "@raycast/api";
import { execFile } from "child_process";
import { existsSync, readFileSync, statSync } from "fs";
import { homedir } from "os";
import { join } from "path";

// One recording or screenshot from QuickRecord's history (index.json, newest first).
export interface Entry {
  id: string;
  kind: "recording" | "screenshot";
  created: string; // ISO date, local time
  master: string; // the original MP4 / PNG
  thumb: string; // screenshot thumbnail, or a recording's sprite sheet
  exports: string[]; // saved files, oldest first
  width: number;
  height: number;
  duration: number; // seconds; 0 for screenshots
}

const dataDir = join(homedir(), "Library/Application Support/quickrecord");

export function loadEntries(kind: Entry["kind"]): Entry[] {
  try {
    const index = JSON.parse(readFileSync(join(dataDir, "index.json"), "utf8"));
    const entries: Entry[] = index.recordings ?? [];
    return entries
      .filter((e) => e.kind === kind && existsSync(e.master))
      .sort((a, b) => b.created.localeCompare(a.created));
  } catch {
    return [];
  }
}

// What to paste or copy: the newest saved export (it has the edits), else the original.
export function entryFile(e: Entry): string {
  const saved = [...e.exports].reverse().find((path) => existsSync(path));
  return saved ?? e.master;
}

// What to show in a grid: a screenshot's thumbnail; a recording's newest GIF (Raycast animates
// it), else its sprite sheet of frames.
export function entryImage(e: Entry): string {
  if (e.kind === "recording") {
    const gif = [...e.exports].reverse().find((path) => path.endsWith(".gif") && existsSync(path));
    if (gif) return gif;
  }
  return existsSync(e.thumb) ? e.thumb : entryFile(e);
}

export function entryTitle(e: Entry): string {
  const date = new Date(e.created);
  return date.toLocaleString(undefined, { month: "short", day: "numeric", hour: "2-digit", minute: "2-digit" });
}

export function entrySubtitle(e: Entry): string {
  const size = `${e.width}×${e.height}`;
  const file = entryFile(e);
  const bytes = existsSync(file) ? statSync(file).size : 0;
  const mb = bytes > 0 ? ` · ${(bytes / 1024 / 1024).toFixed(1)} MB` : "";
  return e.kind === "recording" ? `${e.duration.toFixed(1)} s · ${size}${mb}` : `${size}${mb}`;
}

function run(file: string, args: string[]): Promise<boolean> {
  return new Promise((resolve) => execFile(file, args, (error) => resolve(!error)));
}

// Sends a command-line option (e.g. "--toggle") to QuickRecord. A running instance gets it
// forwarded; otherwise the app is launched through LaunchServices, so macOS applies the
// permissions granted to QuickRecord (not to Raycast).
export async function quickRecord(...args: string[]): Promise<void> {
  const { appPath } = getPreferenceValues<{ appPath: string }>();
  const binary = join(appPath, "Contents/MacOS/QuickRecord");
  if (!existsSync(binary)) {
    await showHUD(`QuickRecord not found at ${appPath} — set its location in the extension's preferences`);
    return;
  }
  const running = await run("/usr/bin/pgrep", ["-f", "QuickRecord.app/Contents/MacOS/QuickRecord"]);
  const ok = running ? await run(binary, args) : await run("/usr/bin/open", ["-a", appPath, "--args", ...args]);
  if (!ok) await showHUD("Could not reach QuickRecord");
}

// For the no-view commands: get Raycast's window out of the way (and out of the picture).
export async function command(...args: string[]): Promise<void> {
  await closeMainWindow();
  await quickRecord(...args);
}
