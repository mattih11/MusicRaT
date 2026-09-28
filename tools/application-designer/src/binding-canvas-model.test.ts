import { describe, expect, it } from 'vitest'
import { demoApplication, demoDescriptors } from './demo'
import {
  bindingFromConnection,
  ownerNodeId,
  projectBindingCanvas,
  targetNodeId,
} from './binding-canvas-model'
import { importApplication } from './model'

function fixture() {
  const workspace = importApplication(structuredClone(demoApplication), demoDescriptors)
  const control = workspace.source.musicrat_control!
  return { workspace, control }
}

describe('binding canvas model', () => {
  it('projects semantic owners and controllable module parameters', () => {
    const { workspace, control } = fixture()
    const graph = projectBindingCanvas(control, workspace.modules, {
      [ownerNodeId('virtual-controls')]: { x: 25, y: 40 },
    })

    expect(graph.nodes.find((node) => node.id === ownerNodeId('virtual-controls')))
      .toMatchObject({ position: { x: 25, y: 40 }, data: { kind: 'owner' } })
    expect(graph.nodes.find((node) => node.id === targetNodeId('Instrument_1')))
      .toMatchObject({ data: { kind: 'target', parameters: [{ id: 1, name: 'gain' }] } })
    expect(graph.nodes.some((node) => node.id === targetNodeId('WavSink_1'))).toBe(false)
  })

  it('creates a persistent binding from a compatible graph connection', () => {
    const { workspace, control } = fixture()
    const graph = projectBindingCanvas(control, workspace.modules)
    const binding = bindingFromConnection(control, workspace.modules, graph.nodes, {
      source: ownerNodeId('virtual-controls'),
      sourceHandle: 'gain',
      target: targetNodeId('Instrument_1'),
      targetHandle: '1',
    })

    expect(binding).toMatchObject({
      id: 'binding-1',
      source: { owner_id: 'virtual-controls', endpoint_id: 'gain' },
      target: { module_id: 'Instrument_1', parameter_id: 1 },
      mode: 'absolute',
    })
  })

  it('projects binding semantics and rejects reversed connections', () => {
    const { workspace, control } = fixture()
    control.bindings.push({
      id: 'gain-control',
      source: { owner_id: 'virtual-controls', endpoint_id: 'gain' },
      target: { module_id: 'Instrument_1', parameter_id: 1 },
      mode: 'absolute',
      pickup: 'match',
      feedback: { owner_id: 'virtual-controls', endpoint_id: 'gain' },
    })
    const graph = projectBindingCanvas(control, workspace.modules)

    expect(graph.edges[0]).toMatchObject({
      id: 'gain-control',
      label: 'absolute · pickup',
      className: 'binding-edge has-feedback',
      markerStart: { type: 'arrowclosed' },
    })
    expect(bindingFromConnection(control, workspace.modules, graph.nodes, {
      source: targetNodeId('Instrument_1'),
      sourceHandle: '1',
      target: ownerNodeId('virtual-controls'),
      targetHandle: 'gain',
    })).toBeUndefined()
  })
})
