import { Clipboard, showHUD } from "@raycast/api";
import { basename } from "path";
import { entryFile, loadEntries } from "./quickrecord";

export default async function Command() {
  const [last] = loadEntries("screenshot");
  if (!last) {
    await showHUD("No QuickRecord screenshots yet");
    return;
  }
  const file = entryFile(last);
  await Clipboard.copy({ file });
  await showHUD(`Copied ${basename(file)}`);
}
