import { describe, expect, it } from 'vitest'
import { demoDescriptors } from './demo'
import {
  isImplementationModule,
  moduleDisplayName,
  modulePickerEntries,
} from './module-picker-model'

describe('module picker model', () => {
  it('hides generated adapters and test-only modules by default', () => {
    const internalClasses = [
      'MusicRaTControlMapper',
      'MusicRaTChannelMapper',
      'MusicRaTDeckControlSource',
      'MusicRaTTransportSource',
      'MusicRaTNullSink',
      'MusicRaTPlaybackStatusUiAdapter',
    ]
    const internals = internalClasses.map((module_class) => ({
      ...demoDescriptors[0],
      module_class,
    }))
    const catalog = [...demoDescriptors, ...internals]

    for (const moduleClass of internalClasses) {
      expect(modulePickerEntries(catalog, '', false).map((item) => item.module_class))
        .not.toContain(moduleClass)
    }
    expect(modulePickerEntries(catalog, '', true).map((item) => item.module_class))
      .toEqual(expect.arrayContaining(internalClasses))
    expect(internals.every(isImplementationModule)).toBe(true)
  })

  it('keeps user-facing modules searchable by readable names', () => {
    const oscillator = {
      ...demoDescriptors[0],
      module_class: 'MusicRaTSineOscillator',
    }

    expect(moduleDisplayName(oscillator)).toBe('Sine Oscillator')
    expect(modulePickerEntries([oscillator], 'sine osc', false)).toEqual([oscillator])
  })
})
