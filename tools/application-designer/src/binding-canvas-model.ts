import { MarkerType, type Edge, type Node } from '@xyflow/react'
import { nextBindingId } from './binding-editor-model'
import {
  bindingModesForEndpoint,
  parametersOf,
  type ControlBinding,
  type ControlEndpoint,
  type DesignerModule,
  type DesignerPosition,
  type MusicRaTControlProject,
} from './model'

export interface BindingOwnerNodeData extends Record<string, unknown> {
  kind: 'owner'
  owner_id: string
  display_name: string
  owner_kind: string
  endpoints: ControlEndpoint[]
}

export interface BindingTargetNodeData extends Record<string, unknown> {
  kind: 'target'
  module_id: string
  display_name: string
  module_class: string
  parameters: ReturnType<typeof parametersOf>
}

export type BindingCanvasNode = Node<
  BindingOwnerNodeData | BindingTargetNodeData,
  'bindingOwner' | 'bindingTarget'
>

export interface BindingEdgeData extends Record<string, unknown> {
  binding_id: string
  feedback: boolean
}

export type BindingCanvasEdge = Edge<BindingEdgeData>

export function ownerNodeId(ownerId: string): string {
  return `owner:${encodeURIComponent(ownerId)}`
}

export function targetNodeId(moduleId: string): string {
  return `target:${encodeURIComponent(moduleId)}`
}

function positionFor(
  positions: Record<string, DesignerPosition> | undefined,
  id: string,
  fallback: DesignerPosition,
): DesignerPosition {
  return positions?.[id] ?? fallback
}

export function projectBindingCanvas(
  control: MusicRaTControlProject | undefined,
  modules: DesignerModule[],
  positions?: Record<string, DesignerPosition>,
): { nodes: BindingCanvasNode[]; edges: BindingCanvasEdge[] } {
  const owners = control ? [...control.devices, ...control.surfaces] : []
  const ownerNodes: BindingCanvasNode[] = owners
    .map((owner) => ({
      owner,
      endpoints: owner.endpoints.filter((endpoint) =>
        bindingModesForEndpoint(endpoint).length > 0),
    }))
    .filter(({ endpoints }) => endpoints.length > 0)
    .map(({ owner, endpoints }, index) => {
      const id = ownerNodeId(owner.id)
      return {
        id,
        type: 'bindingOwner',
        position: positionFor(positions, id, { x: 60, y: 80 + index * 180 }),
        deletable: false,
        data: {
          kind: 'owner',
          owner_id: owner.id,
          display_name: owner.display_name,
          owner_kind: 'kind' in owner ? owner.kind : owner.target,
          endpoints,
        },
      }
    })
  const targetModules = modules.map((module) => ({
    module,
    parameters: parametersOf(module.descriptor)
      .filter((parameter) => parameter.automatable && !parameter.read_only),
  })).filter(({ parameters }) => parameters.length > 0)
  const targetNodes: BindingCanvasNode[] = targetModules.map(({ module, parameters }, index) => {
    const id = targetNodeId(module.id)
    return {
      id,
      type: 'bindingTarget',
      position: positionFor(positions, id, {
        x: Math.max(520, module.position.x + 360),
        y: module.position.y || 80 + index * 180,
      }),
      deletable: false,
      data: {
        kind: 'target',
        module_id: module.id,
        display_name: module.id,
        module_class: module.descriptor.module_class,
        parameters,
      },
    }
  })
  const edges: BindingCanvasEdge[] = (control?.bindings ?? []).map((binding) => ({
    id: binding.id,
    source: ownerNodeId(binding.source.owner_id),
    sourceHandle: binding.source.endpoint_id,
    target: targetNodeId(binding.target.module_id),
    targetHandle: String(binding.target.parameter_id),
    label: `${binding.mode}${binding.pickup === 'match' ? ' · pickup' : ''}`,
    className: binding.feedback ? 'binding-edge has-feedback' : 'binding-edge',
    markerStart: binding.feedback ? { type: MarkerType.ArrowClosed } : undefined,
    data: { binding_id: binding.id, feedback: Boolean(binding.feedback) },
  }))
  return { nodes: [...ownerNodes, ...targetNodes], edges }
}

export function bindingFromConnection(
  control: MusicRaTControlProject,
  modules: DesignerModule[],
  nodes: BindingCanvasNode[],
  connection: {
    source?: string | null
    sourceHandle?: string | null
    target?: string | null
    targetHandle?: string | null
  },
): ControlBinding | undefined {
  const source = nodes.find((node) => node.id === connection.source)?.data
  const target = nodes.find((node) => node.id === connection.target)?.data
  if (source?.kind !== 'owner' || target?.kind !== 'target'
      || !connection.sourceHandle || !connection.targetHandle) return undefined
  const endpoint = source.endpoints.find((item) => item.id === connection.sourceHandle)
  const parameterId = Number(connection.targetHandle)
  const module = modules.find((item) => item.id === target.module_id)
  const parameter = module && parametersOf(module.descriptor).find((item) =>
    item.id === parameterId && item.automatable && !item.read_only)
  if (!endpoint || !parameter) return undefined
  return {
    id: nextBindingId(control.bindings),
    source: { owner_id: source.owner_id, endpoint_id: endpoint.id },
    target: { module_id: target.module_id, parameter_id: parameter.id },
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
  }
}
