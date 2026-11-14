import { launchLinks } from "../../platform/launchLinks";
import { takeAnimationBursts, watchAnimationBursts } from "./animationBursts";
import { checkDefaultBrowserAtLaunch, takeDefaultBrowserCounts } from "./defaultBrowserCounts";
import { takeIntroCounts } from "./introCounts";
import { takeWindowDrags, watchWindowDrags } from "./windowDrags";

export function watchPlatformCounts() {
  watchAnimationBursts();
  watchWindowDrags();
  checkDefaultBrowserAtLaunch();
}

export const takePlatformCounts = () => ({ ...takeAnimationBursts(), ...takeWindowDrags(), ...takeDefaultBrowserCounts(), ...takeIntroCounts(), launch_command_line_links: launchLinks.takeOpenedAtLaunch() });
