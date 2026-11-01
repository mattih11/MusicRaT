import { controllableParameters } from './binding-editor-model'
import {
  actionsOf, bindingModesForEndpoint, observablesOf, payloadType, portsOf,
  type ActionDescriptor, type ControlEndpoint, type DesignerModule,
  type ObservableDescriptor, type SurfaceWidget,
} from './model'
import { uiComponentDefinition } from './ui-component-catalog'
import { playbackObservationProperty } from './playback-observations'

export function parameterTargets(modules: DesignerModule[]) {
  return modules.flatMap((module) => controllableParameters(module.descriptor)
    .map((parameter) => ({ module, parameter })))
}

export function supportsParameterState(module: DesignerModule): boolean {
  return portsOf(module.descriptor).filter((port) => port.direction === 'output'
    && payloadType(module.descriptor, port) === 'CommRaT::Messages::ParameterStateBlock').length === 1
}

export function actionAcceptsEndpoint(action: ActionDescriptor, endpoint: ControlEndpoint): boolean {
  return bindingModesForEndpoint(endpoint).length > 0 && (action.kind === endpoint.domain
    || action.kind === 'trigger' && ['trigger', 'boolean', 'gate'].includes(endpoint.domain)
    || action.kind === 'continuous' && ['unipolar', 'bipolar', 'relative'].includes(endpoint.domain))
}

export function actionTargets(modules: DesignerModule[], endpoint?: ControlEndpoint) {
  return modules.flatMap((module) => actionsOf(module.descriptor)
    .filter((action) => (!endpoint || actionAcceptsEndpoint(action, endpoint))
      && portsOf(module.descriptor).some((port) => port.id === action.input_port_id
        && ['input', 'synced_input'].includes(port.direction)
        && payloadType(module.descriptor, port) === 'CommRaT::Messages::DeckControlEventBlock'))
    .map((action) => ({ module, action })))
}

export function observableTargets(modules: DesignerModule[]) {
  return modules.flatMap((module) => observablesOf(module.descriptor)
    .filter((observable) => portsOf(module.descriptor).some((port) =>
      port.id === observable.output_port_id && port.direction === 'output'
      && (payloadType(module.descriptor, port) === 'CommRaT::Messages::LevelMeterBlock'
        && ['peak', 'rms', 'clipped'].includes(observable.selector)
        || payloadType(module.descriptor, port) === 'CommRaT::Messages::PlaybackStatusBlock'
        && playbackObservationProperty(observable.selector) !== undefined)))
    .map((observable) => ({ module, observable })))
}

export function observableFeedbackProperty(observable: ObservableDescriptor): 'value' | 'active' | 'text' | undefined {
  if (['peak', 'rms'].includes(observable.selector)) return 'value'
  if (observable.selector === 'clipped') return 'active'
  return playbackObservationProperty(observable.selector)
}

export function widgetAcceptsObservable(widget: SurfaceWidget, observable: ObservableDescriptor): boolean {
  if (widgetFeedbackProperty(widget) !== observableFeedbackProperty(observable)) return false
  if (observable.kind === 'choice') return widget.kind === 'choice'
  if (observable.kind === 'continuous') return ['meter', 'slider', 'fader', 'knob'].includes(widget.kind)
  return observable.kind === 'boolean' || observable.kind === 'text'
}

export function widgetFeedbackProperty(widget: SurfaceWidget): 'value' | 'active' | 'text' {
  return widget.kind === 'text' ? 'text'
    : ['button', 'toggle'].includes(widget.kind) ? 'active' : 'value'
}

export function widgetAcceptsFeedback(widget: SurfaceWidget): boolean {
  return widget.kind !== 'keyboard'
    && (widget.route !== 'control' || uiComponentDefinition(widget.kind)?.domain !== undefined)
}
