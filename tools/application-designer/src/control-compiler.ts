import {
  compileApplication,
  parametersOf,
  payloadType,
  portsOf,
  validateControlProject,
  type Address,
  type ApplicationDocument,
  type ApplicationModule,
  type ControlBinding,
  type ControlEndpoint,
  type ControlValueDomain,
  type DesignerWorkspace,
  type MusicRaTControlProject,
  type ParameterDescriptor,
} from './model'

const controlKinds: Partial<Record<ControlValueDomain, number>> = {
  unipolar: 0,
  bipolar: 1,
  relative: 2,
  boolean: 3,
  choice: 4,
  gate: 5,
  trigger: 6,
}

const mappingModes: Record<ControlBinding['mode'], number> = {
  absolute: 0,
  relative: 1,
  toggle: 2,
  momentary: 3,
  gate: 4,
  trigger: 5,
  choice: 6,
}

const mappingCurves = { linear: 0, logarithmic: 1, exponential: 2 } as const
const pickupModes = { immediate: 0, match: 1 } as const

export interface RuntimeIdEntry {
  project_id: string
  runtime_id: number
}

export interface RuntimeEndpointIdEntry extends RuntimeIdEntry {
  owner_id: string
}

export interface CompiledControlBinding {
  binding_id: number
  source_device_id: number
  source_endpoint_id: number
  target_parameter_id: number
  source_kind: number
  mode: number
  curve: number
  pickup: number
  source_minimum: number
  source_maximum: number
  target_minimum: number
  target_maximum: number
  scale: number
  offset: number
  dead_zone: number
  quantization: number
  hysteresis: number
  pickup_tolerance: number
  initial_value: number
  invert: boolean
}

export interface CompiledControlMapperModule {
  module_id: string
  source_owner_id: string
  source_module_id: string
  target_module_id: string
  params: { bindings: CompiledControlBinding[] }
}

export interface CompiledControlPlan {
  schema_version: 1
  owner_ids: RuntimeIdEntry[]
  endpoint_ids: RuntimeEndpointIdEntry[]
  binding_ids: RuntimeIdEntry[]
  mappers: CompiledControlMapperModule[]
}

function allocate(ids: string[]): Map<string, number> {
  return new Map([...ids].sort().map((id, index) => [id, index + 1]))
}

function endpointKey(ownerId: string, endpointId: string): string {
  return `${ownerId}\u0000${endpointId}`
}

function sourceRange(endpoint: ControlEndpoint): [number, number] {
  const defaults: Partial<Record<ControlValueDomain, [number, number]>> = {
    unipolar: [0, 1],
    bipolar: [-1, 1],
    relative: [-1, 1],
    boolean: [0, 1],
    choice: [0, Math.max(0, (endpoint.choices?.length ?? 1) - 1)],
    gate: [0, 1],
    trigger: [0, 1],
  }
  const fallback = defaults[endpoint.domain]
  if (!fallback) throw new Error(`Endpoint '${endpoint.id}' cannot drive a parameter mapping.`)
  return [endpoint.minimum ?? fallback[0], endpoint.maximum ?? fallback[1]]
}

function initialValue(
  workspace: DesignerWorkspace,
  moduleId: string,
  parameter: ParameterDescriptor,
): number {
  const module = workspace.modules.find((item) => item.id === moduleId)!
  const value = module.source.params?.[parameter.name]
    ?? module.descriptor.params_defaults?.[parameter.name]
  if (typeof value === 'boolean') return value ? 1 : 0
  return typeof value === 'number' && Number.isFinite(value)
    ? value
    : parameter.minimum
}

function projectOwner(
  control: MusicRaTControlProject,
  ownerId: string,
) {
  return [...control.devices, ...control.surfaces]
    .find((owner) => owner.id === ownerId)!
}

export function compileControlPlan(
  workspace: DesignerWorkspace,
  options: { maxBindingsPerMapper?: number } = {},
): CompiledControlPlan {
  const control = workspace.source.musicrat_control
  if (!control) {
    return {
      schema_version: 1,
      owner_ids: [],
      endpoint_ids: [],
      binding_ids: [],
      mappers: [],
    }
  }
  const issues = validateControlProject(control, workspace.modules)
  if (issues.length > 0) throw new Error(issues[0].message)

  const owners = [...control.devices, ...control.surfaces]
  const ownerIds = allocate(owners.map((owner) => owner.id))
  const endpointIds = allocate(owners.flatMap((owner) => owner.endpoints
    .map((endpoint) => endpointKey(owner.id, endpoint.id))))
  const bindingIds = allocate(control.bindings.map((binding) => binding.id))
  const groups = new Map<string, CompiledControlMapperModule>()

  for (const binding of [...control.bindings].sort((left, right) => left.id.localeCompare(right.id))) {
    const owner = projectOwner(control, binding.source.owner_id)
    const sourceModuleId = 'adapter_module_id' in owner ? owner.adapter_module_id : undefined
    if (!sourceModuleId || !workspace.modules.some((module) => module.id === sourceModuleId)) {
      throw new Error(`Binding '${binding.id}' requires a source owner with an adapter module.`)
    }
    const endpoint = owner.endpoints.find((item) => item.id === binding.source.endpoint_id)!
    const kind = controlKinds[endpoint.domain]
    if (kind === undefined) {
      throw new Error(`Endpoint '${endpoint.id}' cannot drive a parameter mapping.`)
    }
    const targetModule = workspace.modules.find(
      (module) => module.id === binding.target.module_id)!
    const parameter = parametersOf(targetModule.descriptor).find(
      (item) => item.id === binding.target.parameter_id)!
    const [sourceMinimum, sourceMaximum] = sourceRange(endpoint)
    const transform = binding.transform ?? {}
    const groupKey = `${owner.id}\u0000${targetModule.id}`
    let group = groups.get(groupKey)
    if (!group) {
      group = {
        module_id: `ControlMapper_${groups.size + 1}`,
        source_owner_id: owner.id,
        source_module_id: sourceModuleId,
        target_module_id: targetModule.id,
        params: { bindings: [] },
      }
      groups.set(groupKey, group)
    }
    group.params.bindings.push({
      binding_id: bindingIds.get(binding.id)!,
      source_device_id: ownerIds.get(owner.id)!,
      source_endpoint_id: endpointIds.get(endpointKey(owner.id, endpoint.id))!,
      target_parameter_id: parameter.id,
      source_kind: kind,
      mode: mappingModes[binding.mode],
      curve: mappingCurves[transform.curve ?? 'linear'],
      pickup: pickupModes[binding.pickup ?? 'immediate'],
      source_minimum: sourceMinimum,
      source_maximum: sourceMaximum,
      target_minimum: parameter.minimum,
      target_maximum: parameter.maximum,
      scale: transform.scale ?? 1,
      offset: transform.offset ?? 0,
      dead_zone: transform.dead_zone ?? 0,
      quantization: transform.quantization ?? 0,
      hysteresis: transform.hysteresis ?? 0,
      pickup_tolerance: binding.pickup_tolerance ?? 0.01,
      initial_value: initialValue(workspace, targetModule.id, parameter),
      invert: transform.invert ?? false,
    })
  }

  const maxBindings = options.maxBindingsPerMapper ?? 256
  for (const group of groups.values()) {
    if (group.params.bindings.length > maxBindings) {
      throw new Error(`Mapper '${group.module_id}' exceeds its binding capacity.`)
    }
  }

  return {
    schema_version: 1,
    owner_ids: [...ownerIds].map(([project_id, runtime_id]) => ({ project_id, runtime_id })),
    endpoint_ids: [...endpointIds].map(([key, runtime_id]) => {
      const [owner_id, project_id] = key.split('\u0000')
      return { owner_id, project_id, runtime_id }
    }),
    binding_ids: [...bindingIds].map(([project_id, runtime_id]) => ({ project_id, runtime_id })),
    mappers: [...groups.values()],
  }
}

const controlEventPayload = 'CommRaT::Messages::ControlEventBlock'
const parameterEventPayload = 'CommRaT::Messages::ParameterEventBlock'

function onlyPortIndex(
  workspace: DesignerWorkspace,
  moduleId: string,
  direction: 'output' | 'synced_input',
  payload: string,
): number {
  const module = workspace.modules.find((item) => item.id === moduleId)!
  const matches = portsOf(module.descriptor).filter((port) =>
    port.direction === direction && payloadType(module.descriptor, port) === payload)
  if (matches.length !== 1) {
    throw new Error(`Module '${moduleId}' must expose exactly one ${payload} ${direction}.`)
  }
  return matches[0].port_index
}

export function compileLaunchApplication(
  workspace: DesignerWorkspace,
  options: { maxBindingsPerMapper?: number } = {},
): ApplicationDocument {
  const application = compileApplication(workspace)
  const plan = compileControlPlan(workspace, options)
  if (plan.mappers.length === 0) return application

  const moduleNames = new Set(application.modules.map((module) => module.name))
  for (const mapper of plan.mappers) {
    if (moduleNames.has(mapper.module_id)) {
      throw new Error(`Generated mapper name '${mapper.module_id}' conflicts with a module.`)
    }
  }

  const groupsByTarget = new Map<string, number>()
  for (const mapper of plan.mappers) {
    groupsByTarget.set(
      mapper.target_module_id,
      (groupsByTarget.get(mapper.target_module_id) ?? 0) + 1)
  }
  for (const [target, count] of groupsByTarget) {
    if (count > 1) {
      throw new Error(
        `Target '${target}' has bindings from multiple control owners; add a control bus before export.`,
      )
    }
  }

  let nextInstance = application.modules.flatMap((module) => module.outputs ?? [])
    .filter((address) => address.system_id === 10)
    .reduce((maximum, address) => Math.max(maximum, address.instance_id), 0) + 1
  const generated: ApplicationModule[] = []
  for (const mapper of plan.mappers) {
    if (nextInstance > 255) throw new Error('Generated mapper addresses exceed uint8 capacity.')
    const sourceModule = application.modules.find(
      (module) => module.name === mapper.source_module_id)!
    const sourcePortIndex = onlyPortIndex(
      workspace, mapper.source_module_id, 'output', controlEventPayload)
    const sourceAddress = sourceModule.outputs?.[sourcePortIndex]
    if (!sourceAddress) {
      throw new Error(`Control source '${mapper.source_module_id}' has no output address.`)
    }

    const targetModule = application.modules.find(
      (module) => module.name === mapper.target_module_id)!
    const targetPortIndex = onlyPortIndex(
      workspace, mapper.target_module_id, 'synced_input', parameterEventPayload)
    const targetRoutes = [...(targetModule.synced_inputs ?? [])]
    if (targetRoutes[targetPortIndex]) {
      throw new Error(`Target '${mapper.target_module_id}' already has a parameter-event route.`)
    }

    const outputAddress: Address = { system_id: 10, instance_id: nextInstance++ }
    targetRoutes[targetPortIndex] = {
      source_system_id: outputAddress.system_id,
      source_instance_id: outputAddress.instance_id,
    }
    targetModule.synced_inputs = targetRoutes
    generated.push({
      name: mapper.module_id,
      module_class: 'MusicRaTControlMapper',
      musicrat_generated: 'control_mapper',
      outputs: [outputAddress],
      inputs: [{
        source_system_id: sourceAddress.system_id,
        source_instance_id: sourceAddress.instance_id,
      }],
      params: mapper.params,
    })
  }
  application.modules = application.modules.flatMap((module) => [
    ...generated.filter((mapper) => plan.mappers.some((entry) =>
      entry.module_id === mapper.name && entry.target_module_id === module.name)),
    module,
  ])
  return application
}