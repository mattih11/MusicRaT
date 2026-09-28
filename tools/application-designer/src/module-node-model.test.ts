import { describe, expect, it } from 'vitest'
import { demoDescriptors } from './demo'
import {
  isAdvancedModulePort,
  modulePortDisplayName,
  visibleModulePorts,
} from './module-node-model'
import { portsOf } from './model'

describe('module node port presentation', () => {
  const instrument = demoDescriptors.find((item) => item.module_class === 'MusicRaTInstrument')!
  const ports = portsOf(instrument)
  const automation = ports.find((port) => port.id === 'parameter_events')!
  const state = ports.find((port) => port.id === 'parameter_state')!
  const audio = ports.find((port) => port.id === 'audio_out')!

  it('distinguishes automation transport from parameter state', () => {
    expect(modulePortDisplayName(instrument, automation)).toBe('Automation Events')
    expect(modulePortDisplayName(instrument, state)).toBe('Parameter State')
    expect(modulePortDisplayName(instrument, audio)).toBe('Audio Out')
  })

  it('collapses only optional parameter transport ports', () => {
    expect(isAdvancedModulePort(instrument, automation)).toBe(true)
    expect(isAdvancedModulePort(instrument, state)).toBe(false)
    expect(isAdvancedModulePort(instrument, audio)).toBe(false)
    expect(visibleModulePorts(instrument, ports, false).map((port) => port.id))
      .not.toContain('parameter_events')
    expect(visibleModulePorts(instrument, ports, true)).toEqual(ports)
  })
})
