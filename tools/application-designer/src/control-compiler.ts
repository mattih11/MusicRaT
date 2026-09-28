import {
  actionsOf,
  compileApplication,
  observablesOf,
  parametersOf,
  payloadType,
  portsOf,
  validateControlProject,
  type Address,
  type ApplicationDocument,
  type ApplicationModule,
  type ActionBinding,
  type ControlBinding,
  type ControlEndpoint,
  type ControlValueDomain,
  type DesignerWorkspace,
  type MusicRaTControlProject,
  type ParameterDescriptor,
} from './model'
import { compileDeckArtifacts } from './deck-compiler'

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

export interface CompiledActionBinding {
  binding_id: number
  source_device_id: number
  source_endpoint_id: number
  source_kind: number
  action_type: number
  quantization: number
  source_minimum: number
  source_maximum: number
  target_minimum: number
  target_maximum: number
  default_value: number
  ramp_frames: number
}

export interface CompiledActionMapperModule {
  module_id: string
  source_owner_id: string
  source_module_id: string
  target_module_id: string
  target_port_id: string
  params: { bindings: CompiledActionBinding[] }
}

export interface CompiledControlFeedbackBinding {
  binding_id: number
  target_parameter_id: number
  destination_device_id: number
  destination_endpoint_id: number
  suppress_origin_id: number
}

export interface CompiledControlFeedbackRouterModule {
  module_id: string
  target_module_id: string
  destination_module_ids: string[]
  params: { bindings: CompiledControlFeedbackBinding[] }
}

export interface CompiledLevelMeterObservationBinding {
  binding_id: number
  surface_id: string
  widget_id: string
  property: number
  selector: number
  channel: number
}

export interface CompiledLevelMeterUiAdapterModule {
  module_id: string
  source_module_id: string
  source_port_id: string
  surface_id: string
  params: { bindings: CompiledLevelMeterObservationBinding[] }
}

export interface CompiledControlPlan {
  schema_version: 1
  owner_ids: RuntimeIdEntry[]
  endpoint_ids: RuntimeEndpointIdEntry[]
  binding_ids: RuntimeIdEntry[]
  observation_binding_ids: RuntimeIdEntry[]
  mappers: CompiledControlMapperModule[]
  action_mappers: CompiledActionMapperModule[]
  observation_adapters: CompiledLevelMeterUiAdapterModule[]
  feedback_routers: CompiledControlFeedbackRouterModule[]
}

const actionQuantization: Record<ActionBinding['quantization'], number> = {
  immediate: 0,
  beat: 1,
  bar: 2,
}
const widgetProperties = { value: 0, active: 1, text: 2 } as const
const levelMeterSelectors = { peak: 0, rms: 1, clipped: 2 } as const

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

function lvglSinkName(
  workspace: DesignerWorkspace,
  surfaceId: string,
): string | undefined {
  const displayIndex = compileDeckArtifacts(workspace)?.lvgl.displays
    .findIndex((display) => display.surface_id === surfaceId) ?? -1
  return displayIndex < 0 ? undefined : `LvglWidgetSink_${displayIndex + 1}`
}

function sourceModuleId(
  workspace: DesignerWorkspace,
  owner: ReturnType<typeof projectOwner>,
): string | undefined {
  if ('adapter_module_id' in owner && owner.adapter_module_id) {
    return workspace.modules.some((module) => module.id === owner.adapter_module_id)
      ? owner.adapter_module_id
      : undefined
  }
  return 'target' in owner && owner.target === 'lvgl'
    ? lvglSinkName(workspace, owner.id)
    : undefined
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
      observation_binding_ids: [],
      mappers: [],
      action_mappers: [],
      observation_adapters: [],
      feedback_routers: [],
    }
  }
  const issues = validateControlProject(control, workspace.modules)
  if (issues.length > 0) throw new Error(issues[0].message)

  const owners = [...control.devices, ...control.surfaces]
  const ownerIds = allocate(owners.map((owner) => owner.id))
  const endpointIds = allocate(owners.flatMap((owner) => owner.endpoints
    .map((endpoint) => endpointKey(owner.id, endpoint.id))))
  const bindingIds = allocate([
    ...control.bindings.map((binding) => binding.id),
    ...(control.action_bindings ?? []).map((binding) => binding.id),
  ])
  const observationBindingIds = allocate(
    (control.observation_bindings ?? []).map((binding) => binding.id))
  const groups = new Map<string, CompiledControlMapperModule>()
  const actionGroups = new Map<string, CompiledActionMapperModule>()
  const observationGroups = new Map<string, CompiledLevelMeterUiAdapterModule>()
  const feedbackGroups = new Map<string, CompiledControlFeedbackRouterModule>()

  for (const binding of [...control.bindings].sort((left, right) => left.id.localeCompare(right.id))) {
    const owner = projectOwner(control, binding.source.owner_id)
    const sourceModule = sourceModuleId(workspace, owner)
    if (!sourceModule) {
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
        source_module_id: sourceModule,
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

    if (binding.feedback) {
      const feedbackOwner = projectOwner(control, binding.feedback.owner_id)
      const destinationModuleId = 'adapter_module_id' in feedbackOwner
        ? feedbackOwner.adapter_module_id
        : undefined
      if (!destinationModuleId
        || !workspace.modules.some((module) => module.id === destinationModuleId)) {
        throw new Error(
          `Binding '${binding.id}' feedback requires a destination owner with an adapter module.`,
        )
      }
      const destinationEndpointId = endpointIds.get(endpointKey(
        binding.feedback.owner_id, binding.feedback.endpoint_id))!
      let feedbackGroup = feedbackGroups.get(targetModule.id)
      if (!feedbackGroup) {
        feedbackGroup = {
          module_id: `ControlFeedbackRouter_${feedbackGroups.size + 1}`,
          target_module_id: targetModule.id,
          destination_module_ids: [],
          params: { bindings: [] },
        }
        feedbackGroups.set(targetModule.id, feedbackGroup)
      }
      if (!feedbackGroup.destination_module_ids.includes(destinationModuleId)) {
        feedbackGroup.destination_module_ids.push(destinationModuleId)
      }
      feedbackGroup.params.bindings.push({
        binding_id: bindingIds.get(binding.id)!,
        target_parameter_id: parameter.id,
        destination_device_id: ownerIds.get(binding.feedback.owner_id)!,
        destination_endpoint_id: destinationEndpointId,
        suppress_origin_id: destinationEndpointId,
      })
    }
  }

  for (const binding of [...(control.action_bindings ?? [])]
    .sort((left, right) => left.id.localeCompare(right.id))) {
    const owner = projectOwner(control, binding.source.owner_id)
    const sourceModule = sourceModuleId(workspace, owner)
    if (!sourceModule) {
      throw new Error(`Action binding '${binding.id}' requires a source owner with an adapter module.`)
    }
    const endpoint = owner.endpoints.find((item) => item.id === binding.source.endpoint_id)!
    const kind = controlKinds[endpoint.domain]
    if (kind === undefined) {
      throw new Error(`Endpoint '${endpoint.id}' cannot drive an action mapping.`)
    }
    const targetModule = workspace.modules.find(
      (module) => module.id === binding.target.module_id)!
    const action = actionsOf(targetModule.descriptor).find(
      (item) => item.id === binding.target.action_id)!
    const [sourceMinimum, sourceMaximum] = sourceRange(endpoint)
    const groupKey = `${owner.id}\u0000${targetModule.id}\u0000${action.input_port_id}`
    let group = actionGroups.get(groupKey)
    if (!group) {
      group = {
        module_id: `ActionMapper_${actionGroups.size + 1}`,
        source_owner_id: owner.id,
        source_module_id: sourceModule,
        target_module_id: targetModule.id,
        target_port_id: action.input_port_id,
        params: { bindings: [] },
      }
      actionGroups.set(groupKey, group)
    }
    group.params.bindings.push({
      binding_id: bindingIds.get(binding.id)!,
      source_device_id: ownerIds.get(owner.id)!,
      source_endpoint_id: endpointIds.get(endpointKey(owner.id, endpoint.id))!,
      source_kind: kind,
      action_type: action.event_type,
      quantization: actionQuantization[binding.quantization],
      source_minimum: sourceMinimum,
      source_maximum: sourceMaximum,
      target_minimum: action.minimum,
      target_maximum: action.maximum,
      default_value: action.default_value,
      ramp_frames: binding.ramp_frames ?? 0,
    })
  }

  for (const binding of [...(control.observation_bindings ?? [])]
    .sort((left, right) => left.id.localeCompare(right.id))) {
    const sourceModule = workspace.modules.find(
      (module) => module.id === binding.source.module_id)!
    const observable = observablesOf(sourceModule.descriptor).find(
      (item) => item.id === binding.source.observable_id)!
    const sourcePort = portsOf(sourceModule.descriptor).find(
      (port) => port.id === observable.output_port_id)!
    const payload = payloadType(sourceModule.descriptor, sourcePort)
    if (payload !== 'CommRaT::Messages::LevelMeterBlock') {
      throw new Error(
        `Observation '${binding.id}' uses unsupported typed payload '${payload ?? 'unknown'}'.`,
      )
    }
    const selector = levelMeterSelectors[
      observable.selector as keyof typeof levelMeterSelectors]
    if (selector === undefined) {
      throw new Error(
        `Observation '${binding.id}' uses unsupported LevelMeter selector '${observable.selector}'.`,
      )
    }
    const property = widgetProperties[binding.target.property]
    if ((selector === levelMeterSelectors.clipped) !== (property === widgetProperties.active)) {
      throw new Error(
        `Observation '${binding.id}' property is incompatible with '${observable.selector}'.`,
      )
    }
    const groupKey = `${sourceModule.id}\u0000${sourcePort.id}\u0000${binding.target.surface_id}`
    let group = observationGroups.get(groupKey)
    if (!group) {
      group = {
        module_id: `LevelMeterUiAdapter_${observationGroups.size + 1}`,
        source_module_id: sourceModule.id,
        source_port_id: sourcePort.id,
        surface_id: binding.target.surface_id,
        params: { bindings: [] },
      }
      observationGroups.set(groupKey, group)
    }
    group.params.bindings.push({
      binding_id: observationBindingIds.get(binding.id)!,
      surface_id: binding.target.surface_id,
      widget_id: binding.target.widget_id,
      property,
      selector,
      channel: binding.source.channel ?? 0,
    })
  }

  const maxBindings = options.maxBindingsPerMapper ?? 256
  for (const group of [
    ...groups.values(), ...actionGroups.values(), ...observationGroups.values(),
  ]) {
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
    observation_binding_ids: [...observationBindingIds]
      .map(([project_id, runtime_id]) => ({ project_id, runtime_id })),
    mappers: [...groups.values()],
    action_mappers: [...actionGroups.values()],
    observation_adapters: [...observationGroups.values()],
    feedback_routers: [...feedbackGroups.values()],
  }
}

const controlEventPayload = 'CommRaT::Messages::ControlEventBlock'
const controlFeedbackPayload = 'CommRaT::Messages::ControlFeedbackBlock'
const parameterEventPayload = 'CommRaT::Messages::ParameterEventBlock'
const parameterStatePayload = 'CommRaT::Messages::ParameterStateBlock'
const deckControlPayload = 'CommRaT::Messages::DeckControlEventBlock'

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

function optionalPortIndex(
  workspace: DesignerWorkspace,
  moduleId: string,
  direction: 'output' | 'synced_input',
  payload: string,
): number | undefined {
  const module = workspace.modules.find((item) => item.id === moduleId)!
  const matches = portsOf(module.descriptor).filter((port) =>
    port.direction === direction && payloadType(module.descriptor, port) === payload)
  if (matches.length > 1) {
    throw new Error(`Module '${moduleId}' must expose at most one ${payload} ${direction}.`)
  }
  return matches[0]?.port_index
}

export function compileLaunchApplication(
  workspace: DesignerWorkspace,
  options: {
    maxBindingsPerMapper?: number
    lvglManifestPath?: string
    lvglBackend?: number
  } = {},
): ApplicationDocument {
  const application = compileApplication(workspace)
  const plan = compileControlPlan(workspace, options)
  if (plan.mappers.length === 0 && plan.action_mappers.length === 0
      && plan.observation_adapters.length === 0) return application

  for (const device of workspace.source.musicrat_control?.devices ?? []) {
    if (!device.adapter_module_id) continue
    const adapter = application.modules.find(
      (module) => module.name === device.adapter_module_id)
    if (adapter?.module_class !== 'MusicRaTControlSource') continue
    if (device.endpoints.length !== 1) {
      throw new Error(`Virtual control source '${device.id}' requires exactly one endpoint.`)
    }
    const endpoint = device.endpoints[0]
    const kind = controlKinds[endpoint.domain]
    if (kind === undefined) {
      throw new Error(`Endpoint '${endpoint.id}' cannot be emitted by a virtual control source.`)
    }
    const endpointId = plan.endpoint_ids.find((entry) =>
      entry.owner_id === device.id && entry.project_id === endpoint.id)!.runtime_id
    adapter.params = {
      ...adapter.params,
      device_id: plan.owner_ids.find((entry) => entry.project_id === device.id)!.runtime_id,
      endpoint_id: endpointId,
      origin_id: endpointId,
      kind,
    }
  }

  const moduleNames = new Set(application.modules.map((module) => module.name))
  for (const mapper of plan.mappers) {
    if (moduleNames.has(mapper.module_id)) {
      throw new Error(`Generated mapper name '${mapper.module_id}' conflicts with a module.`)
    }
  }
  for (const mapper of plan.action_mappers) {
    if (moduleNames.has(mapper.module_id)) {
      throw new Error(`Generated action mapper name '${mapper.module_id}' conflicts with a module.`)
    }
  }
  for (const adapter of plan.observation_adapters) {
    if (moduleNames.has(adapter.module_id)) {
      throw new Error(
        `Generated observation adapter name '${adapter.module_id}' conflicts with a module.`,
      )
    }
  }

  for (const router of plan.feedback_routers) {
    if (moduleNames.has(router.module_id)) {
      throw new Error(`Generated feedback router name '${router.module_id}' conflicts with a module.`)
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
  const deckArtifacts = compileDeckArtifacts(workspace)
  for (const [displayIndex, display] of (deckArtifacts?.lvgl.displays ?? []).entries()) {
    const hasControl = plan.mappers.some(
      (mapper) => mapper.source_owner_id === display.surface_id)
      || plan.action_mappers.some(
        (mapper) => mapper.source_owner_id === display.surface_id)
    const hasObservations = plan.observation_adapters.some(
      (adapter) => adapter.surface_id === display.surface_id)
    if (!hasControl && !hasObservations) continue
    if (nextInstance > 255) {
      throw new Error('Generated LVGL control addresses exceed uint8 capacity.')
    }
    const sinkName = `LvglWidgetSink_${displayIndex + 1}`
    if (moduleNames.has(sinkName)) {
      throw new Error(`Generated LVGL sink name '${sinkName}' conflicts with a module.`)
    }
    const surface = workspace.source.musicrat_control!.surfaces.find(
      (candidate) => candidate.id === display.surface_id)!
    const deviceId = plan.owner_ids.find(
      (entry) => entry.project_id === display.surface_id)!.runtime_id
    const controlBindings = surface.endpoints.flatMap((endpoint) => {
      const kind = controlKinds[endpoint.domain]
      const endpointId = plan.endpoint_ids.find((entry) =>
        entry.owner_id === surface.id && entry.project_id === endpoint.id)?.runtime_id
      return kind === undefined || endpointId === undefined ? [] : [{
        widget_id: endpoint.id,
        endpoint_id: endpointId,
        origin_id: endpointId,
        kind,
      }]
    })
    generated.push({
      name: sinkName,
      module_class: 'MusicRaTLvglWidgetSink',
      musicrat_generated: 'lvgl_widget_sink',
      outputs: [{ system_id: 10, instance_id: nextInstance++ }],
      inputs: [],
      period_ms: 10,
      params: {
        manifest_path: options.lvglManifestPath
          ?? `${workspace.appName || 'musicrat-application'}.lvgl.json`,
        display_instance_id: display.instance_id,
        width: 800,
        height: 480,
        backend: options.lvglBackend ?? 0,
        device_path: '',
        connector_id: -1,
        window_title: deckArtifacts!.lvgl.displays.length === 1
          ? workspace.appName || 'MusicRaT'
          : `${workspace.appName || 'MusicRaT'} - ${display.instance_id}`,
        device_id: deviceId,
        control_bindings: controlBindings,
      },
    })
  }
  for (const mapper of plan.mappers) {
    if (nextInstance > 255) throw new Error('Generated mapper addresses exceed uint8 capacity.')
    const sourceModule = application.modules.find(
      (module) => module.name === mapper.source_module_id)
      ?? generated.find((module) => module.name === mapper.source_module_id)!
    const sourcePortIndex = sourceModule.musicrat_generated === 'lvgl_widget_sink'
      ? 0
      : onlyPortIndex(workspace, mapper.source_module_id, 'output', controlEventPayload)
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
    const statePortIndex = optionalPortIndex(
      workspace, mapper.target_module_id, 'output', parameterStatePayload)
    const stateAddress = statePortIndex === undefined
      ? undefined
      : targetModule.outputs?.[statePortIndex]
    if (statePortIndex !== undefined && !stateAddress) {
      throw new Error(`Target '${mapper.target_module_id}' has no parameter-state output address.`)
    }
    generated.push({
      name: mapper.module_id,
      module_class: 'MusicRaTControlMapper',
      musicrat_generated: 'control_mapper',
      outputs: [outputAddress],
      inputs: [{
        source_system_id: sourceAddress.system_id,
        source_instance_id: sourceAddress.instance_id,
      }],
      ...(stateAddress ? { synced_inputs: [{
        source_system_id: stateAddress.system_id,
        source_instance_id: stateAddress.instance_id,
      }] } : {}),
      params: mapper.params,
    })
  }
  for (const mapper of plan.action_mappers) {
    if (nextInstance > 255) throw new Error('Generated mapper addresses exceed uint8 capacity.')
    const sourceModule = application.modules.find(
      (module) => module.name === mapper.source_module_id)
      ?? generated.find((module) => module.name === mapper.source_module_id)!
    const sourcePortIndex = sourceModule.musicrat_generated === 'lvgl_widget_sink'
      ? 0
      : onlyPortIndex(workspace, mapper.source_module_id, 'output', controlEventPayload)
    const sourceAddress = sourceModule.outputs?.[sourcePortIndex]
    if (!sourceAddress) {
      throw new Error(`Control source '${mapper.source_module_id}' has no output address.`)
    }

    const targetWorkspaceModule = workspace.modules.find(
      (module) => module.id === mapper.target_module_id)!
    const targetPort = portsOf(targetWorkspaceModule.descriptor).find(
      (port) => port.id === mapper.target_port_id)
    if (!targetPort
        || !['input', 'synced_input'].includes(targetPort.direction)
        || payloadType(targetWorkspaceModule.descriptor, targetPort) !== deckControlPayload) {
      throw new Error(
        `Action target '${mapper.target_module_id}' has an invalid deck-control port '${mapper.target_port_id}'.`,
      )
    }
    const targetModule = application.modules.find(
      (module) => module.name === mapper.target_module_id)!
    const routeKey = targetPort.direction === 'input' ? 'inputs' : 'synced_inputs'
    const targetRoutes = [...(targetModule[routeKey] ?? [])]
    if (targetRoutes[targetPort.port_index]) {
      throw new Error(
        `Target '${mapper.target_module_id}' already has a route for '${mapper.target_port_id}'.`,
      )
    }
    const outputAddress: Address = { system_id: 10, instance_id: nextInstance++ }
    targetRoutes[targetPort.port_index] = {
      source_system_id: outputAddress.system_id,
      source_instance_id: outputAddress.instance_id,
    }
    targetModule[routeKey] = targetRoutes
    generated.push({
      name: mapper.module_id,
      module_class: 'MusicRaTActionMapper',
      musicrat_generated: 'action_mapper',
      outputs: [outputAddress],
      inputs: [{
        source_system_id: sourceAddress.system_id,
        source_instance_id: sourceAddress.instance_id,
      }],
      params: mapper.params,
    })
  }
  for (const adapter of plan.observation_adapters) {
    if (nextInstance > 255) {
      throw new Error('Generated observation adapter addresses exceed uint8 capacity.')
    }
    const sourceWorkspaceModule = workspace.modules.find(
      (module) => module.id === adapter.source_module_id)!
    const sourcePort = portsOf(sourceWorkspaceModule.descriptor).find(
      (port) => port.id === adapter.source_port_id)!
    const sourceModule = application.modules.find(
      (module) => module.name === adapter.source_module_id)!
    const sourceAddress = sourceModule.outputs?.[sourcePort.port_index]
    if (!sourceAddress) {
      throw new Error(
        `Observation source '${adapter.source_module_id}' has no output address.`,
      )
    }
    const outputAddress: Address = { system_id: 10, instance_id: nextInstance++ }
    generated.push({
      name: adapter.module_id,
      module_class: 'MusicRaTLevelMeterUiAdapter',
      musicrat_generated: 'observation_adapter',
      outputs: [outputAddress],
      inputs: [{
        source_system_id: sourceAddress.system_id,
        source_instance_id: sourceAddress.instance_id,
      }],
      params: adapter.params,
    })
  }
  let mergerIndex = 1
  for (const [displayIndex, display] of (deckArtifacts?.lvgl.displays ?? []).entries()) {
    const lvglAdapters = plan.observation_adapters.filter(
      (adapter) => adapter.surface_id === display.surface_id)
    if (lvglAdapters.length === 0) continue

    let sourceAddress = generated.find(
      (module) => module.name === lvglAdapters[0].module_id)!.outputs![0]
    for (const adapter of lvglAdapters.slice(1)) {
      if (nextInstance > 255) {
        throw new Error('Generated widget-update merger addresses exceed uint8 capacity.')
      }
      const mergerName = `WidgetUpdateMerger_${mergerIndex++}`
      if (moduleNames.has(mergerName)) {
        throw new Error(`Generated widget-update merger name '${mergerName}' conflicts with a module.`)
      }
      const secondaryAddress = generated.find(
        (module) => module.name === adapter.module_id)!.outputs![0]
      const outputAddress: Address = { system_id: 10, instance_id: nextInstance++ }
      generated.push({
        name: mergerName,
        module_class: 'MusicRaTWidgetUpdateMerger',
        musicrat_generated: 'widget_update_merger',
        outputs: [outputAddress],
        inputs: [{
          source_system_id: sourceAddress.system_id,
          source_instance_id: sourceAddress.instance_id,
        }],
        synced_inputs: [{
          source_system_id: secondaryAddress.system_id,
          source_instance_id: secondaryAddress.instance_id,
        }],
      })
      sourceAddress = outputAddress
    }

    const sink = generated.find(
      (module) => module.name === `LvglWidgetSink_${displayIndex + 1}`)!
    sink.synced_inputs = [{
        source_system_id: sourceAddress.system_id,
        source_instance_id: sourceAddress.instance_id,
      }]
  }
  for (const router of plan.feedback_routers) {
    if (nextInstance > 255) throw new Error('Generated feedback addresses exceed uint8 capacity.')
    const targetModule = application.modules.find(
      (module) => module.name === router.target_module_id)!
    const statePortIndex = onlyPortIndex(
      workspace, router.target_module_id, 'output', parameterStatePayload)
    const stateAddress = targetModule.outputs?.[statePortIndex]
    if (!stateAddress) {
      throw new Error(`Target '${router.target_module_id}' has no parameter-state output address.`)
    }
    const outputAddress: Address = { system_id: 10, instance_id: nextInstance++ }
    for (const destinationModuleId of router.destination_module_ids) {
      const destination = application.modules.find(
        (module) => module.name === destinationModuleId)!
      const feedbackPortIndex = onlyPortIndex(
        workspace, destinationModuleId, 'synced_input', controlFeedbackPayload)
      const routes = [...(destination.synced_inputs ?? [])]
      if (routes[feedbackPortIndex]) {
        throw new Error(`Feedback adapter '${destinationModuleId}' already has a feedback route.`)
      }
      routes[feedbackPortIndex] = {
        source_system_id: outputAddress.system_id,
        source_instance_id: outputAddress.instance_id,
      }
      destination.synced_inputs = routes
    }
    generated.push({
      name: router.module_id,
      module_class: 'MusicRaTControlFeedbackRouter',
      musicrat_generated: 'control_feedback_router',
      outputs: [outputAddress],
      inputs: [{
        source_system_id: stateAddress.system_id,
        source_instance_id: stateAddress.instance_id,
      }],
      params: router.params,
    })
  }
  application.modules = application.modules.flatMap((module) => [
    ...generated.filter((generatedModule) =>
      plan.mappers.some((entry) => entry.module_id === generatedModule.name
        && entry.target_module_id === module.name)
      || plan.action_mappers.some((entry) => entry.module_id === generatedModule.name
        && entry.target_module_id === module.name)
      || plan.feedback_routers.some((entry) => entry.module_id === generatedModule.name
        && entry.target_module_id === module.name)),
    module,
    ...generated.filter((generatedModule) =>
      plan.observation_adapters.some((entry) =>
        entry.module_id === generatedModule.name
        && entry.source_module_id === module.name)),
  ]).concat(generated.filter((module) =>
    module.musicrat_generated === 'widget_update_merger'
    || module.musicrat_generated === 'lvgl_widget_sink'))
  return application
}