import { Clipboard, showHUD } from "@raycast/api";
import { entryFile, loadEntries } from "./quickrecord";

export default async function Command() {
  const [last] = loadEntries("screenshot");
  if (!last) {
    await showHUD("No QuickRecord screenshots yet");
    return;
  }
  await Clipboard.paste({ file: entryFile(last) });
}
