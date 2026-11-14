import { onWindowEvent } from "../../platform/windows";

let drags = 0;
let moves = 0;
let ms = 0;

export function watchWindowDrags() {
  onWindowEvent((e) => {
    if (e.type !== "dragEnded") return;
    drags += 1;
    moves += e.moves;
    ms += e.ms;
  });
}

export function takeWindowDrags() {
  const taken = { window_drags: drags, window_drag_moves: moves, window_drag_ms: ms };
  drags = 0;
  moves = 0;
  ms = 0;
  return taken;
}
