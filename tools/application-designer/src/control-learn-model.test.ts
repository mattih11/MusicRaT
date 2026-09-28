import { describe, expect, it } from 'vitest'
import { demoApplication, demoDescriptors } from './demo'
import {
  confirmLearnProposal,
  createLearnObservation,
  isNewerLearnObservation,
  proposeLearnedBinding,
} from './control-learn-model'
import { importApplication } from './model'

function fixture() {
  const workspace = importApplication(structuredClone(demoApplication), demoDescriptors)
  const control = workspace.source.musicrat_control!
  return { workspace, control }
}

describe('control learn model', () => {
  it('turns an absolute observation into a match-pickup proposal', () => {
    const { workspace, control } = fixture()
    const observation = createLearnObservation(
      control,
      { owner_id: 'virtual-controls', endpoint_id: 'gain' },
      0.72,
      1,
    )!

    expect(proposeLearnedBinding(
      control,
      workspace.modules,
      { module_id: 'Instrument_1', parameter_id: 1 },
      observation,
    )).toMatchObject({
      source_label: 'Virtual Controls / Gain',
      target_label: 'Instrument_1 / Gain',
      binding: { id: 'binding-1', mode: 'absolute', pickup: 'match' },
    })
  })

  it('infers relative and momentary modes from endpoint semantics', () => {
    const { workspace, control } = fixture()
    const endpoint = control.devices[0].endpoints[0]
    endpoint.domain = 'relative'
    endpoint.capabilities = ['relative']
    const relative = createLearnObservation(
      control,
      { owner_id: 'virtual-controls', endpoint_id: 'gain' },
      1,
      2,
    )!
    expect(proposeLearnedBinding(
      control, workspace.modules, { module_id: 'Instrument_1', parameter_id: 1 }, relative,
    )?.binding.mode).toBe('relative')

    endpoint.domain = 'boolean'
    endpoint.capabilities = ['momentary']
    const button = createLearnObservation(
      control,
      { owner_id: 'virtual-controls', endpoint_id: 'gain' },
      1,
      3,
    )!
    expect(proposeLearnedBinding(
      control, workspace.modules, { module_id: 'Instrument_1', parameter_id: 1 }, button,
    )?.binding.mode).toBe('momentary')
  })

  it('adds feedback for a capable motorized endpoint', () => {
    const { workspace, control } = fixture()
    const endpoint = control.devices[0].endpoints[0]
    endpoint.direction = 'bidirectional'
    endpoint.capabilities = ['absolute', 'feedback', 'motorized']
    const observation = createLearnObservation(
      control,
      { owner_id: 'virtual-controls', endpoint_id: 'gain' },
      0.4,
      4,
    )!

    expect(proposeLearnedBinding(
      control, workspace.modules, { module_id: 'Instrument_1', parameter_id: 1 }, observation,
    )?.binding.feedback).toEqual({ owner_id: 'virtual-controls', endpoint_id: 'gain' })
  })

  it('rejects stale provider sequences and confirms exactly one binding', () => {
    const { workspace, control } = fixture()
    const source = { owner_id: 'virtual-controls', endpoint_id: 'gain' }
    const previous = createLearnObservation(control, source, 0.2, 8, 'adapter-1')!
    const stale = createLearnObservation(control, source, 0.7, 7, 'adapter-1')!
    expect(isNewerLearnObservation(previous, stale)).toBe(false)
    expect(isNewerLearnObservation(previous, { ...stale, provider_id: 'adapter-2' })).toBe(true)

    const proposal = proposeLearnedBinding(
      control,
      workspace.modules,
      { module_id: 'Instrument_1', parameter_id: 1 },
      previous,
    )!
    const confirmed = confirmLearnProposal(control, proposal)
    expect(confirmed).not.toBe(control)
    expect(confirmed.bindings).toHaveLength(control.bindings.length + 1)
    expect(control.bindings).toHaveLength(0)
  })

  it('rejects an observation whose domain differs from its endpoint', () => {
    const { workspace, control } = fixture()
    const observation = createLearnObservation(
      control,
      { owner_id: 'virtual-controls', endpoint_id: 'gain' },
      0.5,
      1,
    )!
    observation.domain = 'relative'
    expect(proposeLearnedBinding(
      control,
      workspace.modules,
      { module_id: 'Instrument_1', parameter_id: 1 },
      observation,
    )).toBeUndefined()
  })

  it('rejects feedback-only observations and text targets', () => {
    const { workspace, control } = fixture()
    control.devices[0].endpoints[0].direction = 'output'
    expect(createLearnObservation(
      control,
      { owner_id: 'virtual-controls', endpoint_id: 'gain' },
      1,
      1,
    )).toBeUndefined()

    control.devices[0].endpoints[0].direction = 'input'
    const observation = createLearnObservation(
      control,
      { owner_id: 'virtual-controls', endpoint_id: 'gain' },
      1,
      2,
    )!
    const sink = workspace.modules.find((module) => module.id === 'WavSink_1')!
    sink.descriptor.descriptor_metadata = {
      ...sink.descriptor.descriptor_metadata,
      musicrat_parameters: { schema_version: 1, parameters: [{
        id: 99,
        name: 'path',
        display_name: 'Path',
        group: 'File',
        kind: 'text',
        unit: '',
        minimum: 0,
        maximum: 0,
        step: 0,
        display_scale: 'linear',
        automatable: true,
        read_only: false,
        choices: [],
      }] },
    }
    expect(proposeLearnedBinding(
      control, workspace.modules, { module_id: 'WavSink_1', parameter_id: 99 }, observation,
    )).toBeUndefined()
  })
})
