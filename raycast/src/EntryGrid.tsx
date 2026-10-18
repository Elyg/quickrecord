import { Action, ActionPanel, Clipboard, Grid, Icon, closeMainWindow } from "@raycast/api";
import { basename } from "path";
import { Entry, entryFile, entryImage, entrySubtitle, entryTitle, loadEntries, quickRecord } from "./quickrecord";

// Searchable grid of screenshots or recordings, newest first.
export function EntryGrid({ kind }: { kind: Entry["kind"] }) {
  const entries = loadEntries(kind);
  const noun = kind === "screenshot" ? "screenshots" : "recordings";
  return (
    <Grid
      columns={4}
      aspectRatio="16/9"
      fit={Grid.Fit.Contain}
      searchBarPlaceholder={`Search ${noun} by date or file name`}
    >
      <Grid.EmptyView icon={Icon.Camera} title={`No ${noun} yet`} description="Make one with QuickRecord first." />
      {entries.map((e) => {
        const file = entryFile(e);
        return (
          <Grid.Item
            key={e.id}
            content={{ source: entryImage(e) }}
            title={entryTitle(e)}
            subtitle={entrySubtitle(e)}
            keywords={[e.id, e.created, ...e.exports.map((path) => basename(path))]}
            actions={
              <ActionPanel>
                <Action
                  title="Paste"
                  icon={Icon.Clipboard}
                  onAction={() => Clipboard.paste({ file })}
                />
                <Action.CopyToClipboard title="Copy" content={{ file }} />
                <Action
                  title="Open in QuickRecord Editor"
                  icon={Icon.Pencil}
                  shortcut={{ modifiers: ["cmd"], key: "e" }}
                  onAction={async () => {
                    await closeMainWindow();
                    await quickRecord("--edit", e.id);
                  }}
                />
                <Action.ShowInFinder path={file} />
                <Action.Open title="Open" target={file} shortcut={{ modifiers: ["cmd"], key: "o" }} />
                <Action.CopyToClipboard
                  title="Copy Path"
                  content={file}
                  shortcut={{ modifiers: ["cmd", "shift"], key: "c" }}
                />
              </ActionPanel>
            }
          />
        );
      })}
    </Grid>
  );
}
