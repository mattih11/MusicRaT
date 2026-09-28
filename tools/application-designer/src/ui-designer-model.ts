import { nextBindingId, nextSemanticBindingId } from './binding-editor-model'
import { bindingModesForEndpoint, type ActionTarget, type ControlEndpoint, type MusicRaTControlProject, type ObservableReference, type ParameterTarget, type SurfaceWidget } from './model'
import type { UiComponentDefinition } from './ui-component-catalog'

function nextWidgetId(widgets: SurfaceWidget[], kind: SurfaceWidget['kind']): string {
  let index = 1
  while (widgets.some((widget) => widget.id === `${kind}-${index}`)) index += 1
  return `${kind}-${index}`
}

export function clampWidgetLayout(
  layout: SurfaceWidget['layout'],
  gridSize = 1,
): SurfaceWidget['layout'] {
  const snap = (value: number) => Math.round(value / gridSize) * gridSize
  const width = Math.min(100, Math.max(gridSize, snap(layout.width)))
  const height = Math.min(100, Math.max(gridSize, snap(layout.height)))
  return {
    x: Math.min(100 - width, Math.max(0, snap(layout.x))),
    y: Math.min(100 - height, Math.max(0, snap(layout.y))),
    width,
    height,
  }
}

export function addSurfaceWidget(
  control: MusicRaTControlProject,
  surfaceId: string,
  definition: UiComponentDefinition,
): { control: MusicRaTControlProject; widgetId: string } {
  const surface = control.surfaces.find((item) => item.id === surfaceId)
  if (!surface) return { control, widgetId: '' }
  const widgetId = nextWidgetId(surface.widgets, definition.kind)
  const offset = (surface.widgets.length % 6) * 5
  const widget: SurfaceWidget = {
    id: widgetId,
    kind: definition.kind,
    display_name: definition.name,
    layout: clampWidgetLayout({ x: 5 + offset, y: 5 + offset, width: definition.width, height: definition.height }, 1),
    route: definition.default_route,
    endpoint_id: definition.domain ? widgetId : undefined,
  }
  const endpoint = definition.domain && endpointFor(widget, definition.domain)
  return {
    widgetId,
    control: {
      ...control,
      surfaces: control.surfaces.map((item) => item.id === surfaceId ? {
        ...item,
        widgets: [...item.widgets, widget],
        endpoints: endpoint ? [...item.endpoints, endpoint] : item.endpoints,
      } : item),
    },
  }
}

function endpointFor(widget: SurfaceWidget, domain: NonNullable<UiComponentDefinition['domain']>): ControlEndpoint {
  const acceptsFeedback = widget.route === 'bidirectional'
  return {
    id: widget.id,
    display_name: widget.display_name,
    direction: acceptsFeedback ? 'bidirectional' : 'input',
    domain,
    minimum: domain === 'relative' ? -1 : 0,
    maximum: 1,
    step: 0.01,
    capabilities: acceptsFeedback ? ['absolute', 'feedback'] : undefined,
  }
}

export function updateSurfaceWidget(
  control: MusicRaTControlProject,
  surfaceId: string,
  widgetId: string,
  change: Partial<SurfaceWidget>,
): MusicRaTControlProject {
  return {
    ...control,
    surfaces: control.surfaces.map((surface) => surface.id === surfaceId ? {
      ...surface,
      widgets: surface.widgets.map((widget) => widget.id === widgetId
        ? { ...widget, ...change }
        : widget),
      endpoints: surface.endpoints.map((endpoint) => endpoint.id === widgetId
        ? { ...endpoint, display_name: change.display_name ?? endpoint.display_name }
        : endpoint),
    } : surface),
  }
}

export function routeSurfaceWidget(
  control: MusicRaTControlProject,
  surfaceId: string,
  widgetId: string,
  target: ParameterTarget | undefined,
  route: SurfaceWidget['route'],
  definition: UiComponentDefinition,
): MusicRaTControlProject {
  const surface = control.surfaces.find((item) => item.id === surfaceId)
  const widget = surface?.widgets.find((item) => item.id === widgetId)
  if (!surface || !widget) return control
  const bindings = control.bindings.filter((binding) => binding.id !== widget.binding_id)
  const actionBindings = (control.action_bindings ?? [])
    .filter((binding) => binding.id !== widget.action_binding_id)
  const controlsParameter = route !== 'feedback' && definition.domain && target
  const nextWidget: SurfaceWidget = {
    ...widget,
    route,
    parameter: target,
    action: undefined,
    endpoint_id: route !== 'feedback' && definition.domain ? widget.id : undefined,
    binding_id: controlsParameter ? nextBindingId(bindings) : undefined,
    action_binding_id: undefined,
  }
  const endpoint = route !== 'feedback' && definition.domain
    ? endpointFor(nextWidget, definition.domain)
    : undefined
  const binding = controlsParameter && endpoint ? {
    id: nextWidget.binding_id!,
    source: { owner_id: surfaceId, endpoint_id: widget.id },
    target,
    mode: bindingModesForEndpoint(endpoint)[0]!,
    pickup: definition.domain === 'unipolar' ? 'match' as const : 'immediate' as const,
    feedback: route === 'bidirectional'
      ? { owner_id: surfaceId, endpoint_id: widget.id }
      : undefined,
  } : undefined
  return {
    ...control,
    bindings: binding ? [...bindings, binding] : bindings,
    action_bindings: actionBindings,
    surfaces: control.surfaces.map((item) => item.id === surfaceId ? {
      ...item,
      widgets: item.widgets.map((candidate) => candidate.id === widgetId ? nextWidget : candidate),
      endpoints: [
        ...item.endpoints.filter((candidate) => candidate.id !== widget.id),
        ...(endpoint ? [endpoint] : []),
      ],
    } : item),
  }
}

export function routeSurfaceWidgetAction(
  control: MusicRaTControlProject,
  surfaceId: string,
  widgetId: string,
  target: ActionTarget | undefined,
  route: SurfaceWidget['route'],
  definition: UiComponentDefinition,
): MusicRaTControlProject {
  const surface = control.surfaces.find((item) => item.id === surfaceId)
  const widget = surface?.widgets.find((item) => item.id === widgetId)
  if (!surface || !widget) return control
  const bindings = control.bindings.filter((binding) => binding.id !== widget.binding_id)
  const actionBindings = (control.action_bindings ?? [])
    .filter((binding) => binding.id !== widget.action_binding_id)
  const controlsAction = route !== 'feedback' && definition.domain && target
  const nextWidget: SurfaceWidget = {
    ...widget,
    route,
    parameter: undefined,
    action: target,
    endpoint_id: route !== 'feedback' && definition.domain ? widget.id : undefined,
    binding_id: undefined,
    action_binding_id: controlsAction
      ? nextSemanticBindingId(actionBindings, 'action')
      : undefined,
  }
  const endpoint = route !== 'feedback' && definition.domain
    ? endpointFor(nextWidget, definition.domain)
    : undefined
  return {
    ...control,
    bindings,
    action_bindings: controlsAction ? [...actionBindings, {
      id: nextWidget.action_binding_id!,
      source: { owner_id: surfaceId, endpoint_id: widget.id },
      target,
      quantization: 'immediate',
    }] : actionBindings,
    surfaces: control.surfaces.map((item) => item.id === surfaceId ? {
      ...item,
      widgets: item.widgets.map((candidate) => candidate.id === widgetId ? nextWidget : candidate),
      endpoints: [
        ...item.endpoints.filter((candidate) => candidate.id !== widget.id),
        ...(endpoint ? [endpoint] : []),
      ],
    } : item),
  }
}

export function observeSurfaceWidget(
  control: MusicRaTControlProject,
  surfaceId: string,
  widgetId: string,
  source: ObservableReference | undefined,
): MusicRaTControlProject {
  const surface = control.surfaces.find((item) => item.id === surfaceId)
  const widget = surface?.widgets.find((item) => item.id === widgetId)
  if (!surface || !widget) return control
  const observationBindings = (control.observation_bindings ?? [])
    .filter((binding) => binding.id !== widget.observation_binding_id)
  const bindingId = source ? nextSemanticBindingId(observationBindings, 'observation') : undefined
  return {
    ...control,
    observation_bindings: source ? [...observationBindings, {
      id: bindingId!,
      source,
      target: {
        surface_id: surfaceId,
        widget_id: widgetId,
        property: widget.kind === 'text' ? 'text' : widget.kind === 'button'
          || widget.kind === 'toggle' ? 'active' : 'value',
      },
    }] : observationBindings,
    surfaces: control.surfaces.map((item) => item.id === surfaceId ? {
      ...item,
      widgets: item.widgets.map((candidate) => candidate.id === widgetId ? {
        ...candidate,
        observable: source,
        observation_binding_id: bindingId,
      } : candidate),
    } : item),
  }
}

export function removeSurfaceWidget(
  control: MusicRaTControlProject,
  surfaceId: string,
  widgetId: string,
): MusicRaTControlProject {
  const widget = control.surfaces.find((surface) => surface.id === surfaceId)
    ?.widgets.find((item) => item.id === widgetId)
  return {
    ...control,
    bindings: control.bindings.filter((binding) => binding.id !== widget?.binding_id),
    action_bindings: (control.action_bindings ?? [])
      .filter((binding) => binding.id !== widget?.action_binding_id),
    observation_bindings: (control.observation_bindings ?? [])
      .filter((binding) => binding.id !== widget?.observation_binding_id),
    surfaces: control.surfaces.map((surface) => surface.id === surfaceId ? {
      ...surface,
      widgets: surface.widgets.filter((item) => item.id !== widgetId),
      endpoints: surface.endpoints.filter((endpoint) => endpoint.id !== widgetId),
    } : surface),
  }
}
