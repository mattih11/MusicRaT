import type { ControlValueDomain, SurfaceWidget } from './model'

export interface UiComponentDefinition {
  kind: SurfaceWidget['kind']
  name: string
  width: number
  height: number
  domain?: ControlValueDomain
  default_route: SurfaceWidget['route']
}

export const uiComponentCatalog: UiComponentDefinition[] = [
  { kind: 'knob', name: 'Knob', width: 18, height: 24, domain: 'unipolar', default_route: 'bidirectional' },
  { kind: 'fader', name: 'Fader', width: 14, height: 48, domain: 'unipolar', default_route: 'bidirectional' },
  { kind: 'slider', name: 'Slider', width: 36, height: 14, domain: 'unipolar', default_route: 'bidirectional' },
  { kind: 'button', name: 'Button', width: 22, height: 14, domain: 'trigger', default_route: 'control' },
  { kind: 'toggle', name: 'Toggle', width: 20, height: 14, domain: 'boolean', default_route: 'bidirectional' },
  { kind: 'choice', name: 'Choice', width: 30, height: 14, domain: 'choice', default_route: 'bidirectional' },
  { kind: 'meter', name: 'Meter', width: 14, height: 48, default_route: 'feedback' },
  { kind: 'text', name: 'Value', width: 26, height: 12, default_route: 'feedback' },
]

export function uiComponentDefinition(kind: SurfaceWidget['kind']): UiComponentDefinition | undefined {
  return uiComponentCatalog.find((component) => component.kind === kind)
}
