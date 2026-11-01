// Numeric selectors match protocol/playback_ui.hpp.
export const playbackSelectors = {
  playback_rate: 0,
  media_position_frames: 1,
  duration_frames: 2,
  beat_position: 3,
  'flags.ready': 4,
  'flags.playing': 5,
  'flags.looping': 6,
  worker_state: 7,
  decode_error: 8,
  position: 9,
  elapsed_time: 10,
  remaining_time: 11,
  load_state: 12,
  load_decode_error: 13,
} as const

export function playbackObservationProperty(selector: string): 'value' | 'active' | 'text' | undefined {
  if (!Object.prototype.hasOwnProperty.call(playbackSelectors, selector)) return undefined
  if (selector.startsWith('flags.')) return 'active'
  if (selector === 'elapsed_time' || selector === 'remaining_time') return 'text'
  return 'value'
}
