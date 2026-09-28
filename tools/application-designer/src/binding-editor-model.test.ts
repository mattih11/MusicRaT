import { describe, expect, it } from 'vitest'
import { demoApplication, demoDescriptors } from './demo'
import {
  createDefaultBinding,
  endpointFromKey,
  endpointKey,
  endpointOptions,
  nextBindingId,
  parameterTargetFromKey,
  parameterTargetKey,
  parameterTargetOptions,
} from './binding-editor-model'
import { importApplication, type MusicRaTControlProject } from './model'

const control: MusicRaTControlProject = {
  schema_version: 1,
  devices: [{
    id: 'desk/controller',
    display_name: 'Desk',
    kind: 'hardware',
    endpoints: [{
      id: 'gain/main',
      display_name: 'Gain',
      direction: 'input',
      domain: 'unipolar',
    }],
  }],
  bindings: [],
  surfaces: [],
}

describe('binding editor model', () => {
  it('round-trips endpoint and parameter option keys', () => {
    const reference = { owner_id: 'desk/controller', endpoint_id: 'gain/main' }
    expect(endpointFromKey(endpointKey(reference))).toEqual(reference)
    expect(parameterTargetFromKey(parameterTargetKey('Instrument_1', 1)))
      .toEqual({ module_id: 'Instrument_1', parameter_id: 1 })
  })

  it('lists semantic endpoints and controllable parameters', () => {
    const workspace = importApplication(demoApplication, demoDescriptors)
    expect(endpointOptions(control).map((option) => option.label)).toEqual(['Desk / Gain'])
    expect(parameterTargetOptions(workspace.modules).map((option) => option.label))
      .toEqual(['Instrument_1 / Gain'])
  })

  it('separates control sources from feedback-only endpoints', () => {
    const directional = structuredClone(control)
    directional.devices[0].endpoints.push({
      id: 'led',
      display_name: 'LED',
      direction: 'output',
      domain: 'boolean',
      capabilities: ['feedback'],
    })

    expect(endpointOptions(directional).map((option) => option.label)).toEqual(['Desk / Gain'])
    expect(endpointOptions(directional, 'feedback').map((option) => option.label))
      .toEqual(['Desk / LED'])
  })

  it('creates a deterministic editable binding', () => {
    const workspace = importApplication(demoApplication, demoDescriptors)
    const withExisting = structuredClone(control)
    withExisting.bindings.push({
      id: 'binding-1',
      source: { owner_id: 'desk/controller', endpoint_id: 'gain/main' },
      target: { module_id: 'Instrument_1', parameter_id: 1 },
      mode: 'absolute',
    })

    expect(nextBindingId(withExisting.bindings)).toBe('binding-2')
    expect(createDefaultBinding(withExisting, workspace.modules)).toEqual({
      id: 'binding-2',
      source: { owner_id: 'desk/controller', endpoint_id: 'gain/main' },
      target: { module_id: 'Instrument_1', parameter_id: 1 },
      mode: 'absolute',
      pickup: 'immediate',
      transform: {
        scale: 1,
        offset: 0,
        invert: false,
        dead_zone: 0,
        curve: 'linear',
        quantization: 0,
        hysteresis: 0,
      },
    })
  })
})
