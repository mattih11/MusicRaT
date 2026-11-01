import { describe, expect, it } from 'vitest'
import { playbackObservationProperty, playbackSelectors } from './playback-observations'

describe('playback observation selectors', () => {
  it('preserves the wire selector IDs and includes independent load feedback', () => {
    expect(playbackSelectors).toEqual({
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
    })
    expect(playbackObservationProperty('load_state')).toBe('value')
    expect(playbackObservationProperty('load_decode_error')).toBe('value')
    expect(playbackObservationProperty('track_instance_id')).toBeUndefined()
    expect(playbackObservationProperty('toString')).toBeUndefined()
  })
})
