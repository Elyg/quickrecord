import { command } from "./quickrecord";

export default async function Command() {
  await command("--toggle");
}
