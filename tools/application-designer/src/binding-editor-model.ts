import {
  actionsOf,
  bindingModesForEndpoint,
  observablesOf,
  parametersOf,
  type ActionTarget,
  type ControlBinding,
  type DesignerModule,
  type EndpointReference,
  type MusicRaTControlProject,
  type ObservableReference,
} from './model'

export interface EndpointOption {
  key: string
  label: string
  reference: EndpointReference
}

export interface ParameterTargetOption {
  key: string
  label: string
  module_id: string
  parameter_id: number
}

export interface ActionTargetOption {
  key: string
  label: string
  target: ActionTarget
}

export interface ObservableOption {
  key: string
  label: string
  reference: ObservableReference
  channel_selectable: boolean
}

export function endpointKey(reference: EndpointReference): string {
  return JSON.stringify([reference.owner_id, reference.endpoint_id])
}

export function endpointFromKey(key: string): EndpointReference | undefined {
  try {
    const value = JSON.parse(key) as unknown
    if (!Array.isArray(value) || value.length !== 2
      || typeof value[0] !== 'string' || typeof value[1] !== 'string') return undefined
    return { owner_id: value[0], endpoint_id: value[1] }
  } catch {
    return undefined
  }
}

export function parameterTargetKey(moduleId: string, parameterId: number): string {
  return JSON.stringify([moduleId, parameterId])
}

export function parameterTargetFromKey(
  key: string,
): { module_id: string; parameter_id: number } | undefined {
  try {
    const value = JSON.parse(key) as unknown
    if (!Array.isArray(value) || value.length !== 2
      || typeof value[0] !== 'string' || typeof value[1] !== 'number') return undefined
    return { module_id: value[0], parameter_id: value[1] }
  } catch {
    return undefined
  }
}

export function actionTargetKey(moduleId: string, actionId: string): string {
  return JSON.stringify([moduleId, actionId])
}

export function actionTargetFromKey(key: string): ActionTarget | undefined {
  try {
    const value = JSON.parse(key) as unknown
    if (!Array.isArray(value) || value.length !== 2
      || typeof value[0] !== 'string' || typeof value[1] !== 'string') return undefined
    return { module_id: value[0], action_id: value[1] }
  } catch {
    return undefined
  }
}

export function observableKey(moduleId: string, observableId: string, channel?: number): string {
  return JSON.stringify([moduleId, observableId, channel ?? null])
}

export function observableFromKey(key: string): ObservableReference | undefined {
  try {
    const value = JSON.parse(key) as unknown
    if (!Array.isArray(value) || value.length !== 3
      || typeof value[0] !== 'string' || typeof value[1] !== 'string'
      || (value[2] !== null && typeof value[2] !== 'number')) return undefined
    return { module_id: value[0], observable_id: value[1],
      channel: value[2] === null ? undefined : value[2] }
  } catch {
    return undefined
  }
}

export function endpointOptions(
  control: MusicRaTControlProject,
  role: 'source' | 'feedback' = 'source',
): EndpointOption[] {
  return [...control.devices, ...control.surfaces].flatMap((owner) =>
    owner.endpoints.filter((endpoint) => role === 'source'
      ? bindingModesForEndpoint(endpoint).length > 0
      : endpoint.direction !== 'input').map((endpoint) => {
      const reference = { owner_id: owner.id, endpoint_id: endpoint.id }
      return {
        key: endpointKey(reference),
        label: `${owner.display_name} / ${endpoint.display_name}`,
        reference,
      }
    }))
}

export function parameterTargetOptions(modules: DesignerModule[]): ParameterTargetOption[] {
  return modules.flatMap((module) => parametersOf(module.descriptor)
    .filter((parameter) => parameter.automatable && !parameter.read_only)
    .map((parameter) => ({
      key: parameterTargetKey(module.id, parameter.id),
      label: `${module.id} / ${parameter.display_name}`,
      module_id: module.id,
      parameter_id: parameter.id,
    })))
}

export function actionTargetOptions(modules: DesignerModule[]): ActionTargetOption[] {
  return modules.flatMap((module) => actionsOf(module.descriptor).map((action) => ({
    key: actionTargetKey(module.id, action.id),
    label: `${module.id} / ${action.display_name}`,
    target: { module_id: module.id, action_id: action.id },
  })))
}

export function observableOptions(modules: DesignerModule[]): ObservableOption[] {
  return modules.flatMap((module) => observablesOf(module.descriptor).map((observable) => ({
    key: observableKey(module.id, observable.id),
    label: `${module.id} / ${observable.display_name}`,
    reference: { module_id: module.id, observable_id: observable.id },
    channel_selectable: observable.channel_selectable,
  })))
}

export function nextBindingId(bindings: ControlBinding[]): string {
  let suffix = 1
  while (bindings.some((binding) => binding.id === `binding-${suffix}`)) suffix += 1
  return `binding-${suffix}`
}

export function nextSemanticBindingId(
  bindings: Array<{ id: string }>,
  prefix: 'action' | 'observation',
): string {
  let suffix = 1
  while (bindings.some((binding) => binding.id === `${prefix}-${suffix}`)) suffix += 1
  return `${prefix}-${suffix}`
}

export function createDefaultBinding(
  control: MusicRaTControlProject,
  modules: DesignerModule[],
): ControlBinding | undefined {
  const source = endpointOptions(control)[0]
  const target = parameterTargetOptions(modules)[0]
  if (!source || !target) return undefined
  const owner = [...control.devices, ...control.surfaces]
    .find((item) => item.id === source.reference.owner_id)
  const endpoint = owner?.endpoints.find((item) => item.id === source.reference.endpoint_id)
  const mode = endpoint && bindingModesForEndpoint(endpoint)[0]
  if (!mode) return undefined
  return {
    id: nextBindingId(control.bindings),
    source: source.reference,
    target: { module_id: target.module_id, parameter_id: target.parameter_id },
    mode,
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
