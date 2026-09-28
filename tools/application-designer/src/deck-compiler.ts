import { componentDefinition } from './component-catalog'
import { deckDeviceId } from './deck-editor-model'
import { parametersOf, validateControlProject, type DesignerWorkspace, type ObservationBinding, type ParameterChoice, type SurfaceWidget } from './model'

export interface DriverComponentManifestEntry {
  instance_id: string
  component_id: string
  driver_class: string
  endpoint_id?: string
  surface_id?: string
}

export interface CompiledDeckArtifacts {
  config_fingerprint: string
  driver: {
    schema_version: 1
    config_fingerprint: string
    device_id: string
    panel: { width_mm: number; height_mm: number; thickness_mm: number }
    components: DriverComponentManifestEntry[]
  }
  lvgl: {
    schema_version: 1
    config_fingerprint: string
    observation_bindings: ObservationBinding[]
    displays: Array<{
      instance_id: string
      surface_id: string
      widgets: Array<SurfaceWidget & { choices?: ParameterChoice[] }>
    }>
  }
}

function canonical(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(canonical).join(',')}]`
  if (value !== null && typeof value === 'object') {
    return `{${Object.entries(value).sort(([left], [right]) => left.localeCompare(right))
      .map(([key, item]) => `${JSON.stringify(key)}:${canonical(item)}`).join(',')}}`
  }
  return JSON.stringify(value)
}

function fingerprint(value: unknown): string {
  let hash = 0x811c9dc5
  for (const character of canonical(value)) {
    hash ^= character.charCodeAt(0)
    hash = Math.imul(hash, 0x01000193)
  }
  return `fnv1a32:${(hash >>> 0).toString(16).padStart(8, '0')}`
}

export function compileDeckArtifacts(workspace: DesignerWorkspace): CompiledDeckArtifacts | undefined {
  const control = workspace.source.musicrat_control
  const deck = control?.deck
  if (!control || !deck) return undefined
  const issues = validateControlProject(control, workspace.modules)
  if (issues.length > 0) throw new Error(issues[0].message)

  const components = [...deck.elements].sort((left, right) => left.id.localeCompare(right.id))
    .map((element): DriverComponentManifestEntry => {
      const definition = componentDefinition(element.component_id)
      if (!definition) throw new Error(`Unknown deck component '${element.component_id}'.`)
      return {
        instance_id: element.id,
        component_id: definition.id,
        driver_class: definition.driver_class,
        endpoint_id: definition.endpoint ? element.id : undefined,
        surface_id: definition.kind === 'display' ? `display-${element.id}` : undefined,
      }
    })
  const displays = components.flatMap((component) => {
    if (!component.surface_id) return []
    const surface = control.surfaces.find((item) => item.id === component.surface_id)!
    return [{
      instance_id: component.instance_id,
      surface_id: component.surface_id,
      widgets: [...surface.widgets].sort((left, right) => left.id.localeCompare(right.id))
        .map((widget) => {
          if (widget.kind !== 'choice' || !widget.parameter) return widget
          const module = workspace.modules.find(
            (candidate) => candidate.id === widget.parameter!.module_id)
          const parameter = module && parametersOf(module.descriptor).find(
            (candidate) => candidate.id === widget.parameter!.parameter_id)
          return parameter?.kind === 'choice'
            ? { ...widget, choices: parameter.choices }
            : widget
        }),
    }]
  })
  const displayIds = new Set(displays.map((display) => display.surface_id))
  const observationBindings = [...(control.observation_bindings ?? [])]
    .filter((binding) => displayIds.has(binding.target.surface_id))
    .sort((left, right) => left.id.localeCompare(right.id))
  const source = {
    panel: deck.panel,
    elements: [...deck.elements].sort((left, right) => left.id.localeCompare(right.id)),
    components,
    displays,
    observation_bindings: observationBindings,
  }
  const configFingerprint = fingerprint(source)
  return {
    config_fingerprint: configFingerprint,
    driver: {
      schema_version: 1,
      config_fingerprint: configFingerprint,
      device_id: deckDeviceId,
      panel: deck.panel,
      components,
    },
    lvgl: {
      schema_version: 1,
      config_fingerprint: configFingerprint,
      observation_bindings: observationBindings,
      displays,
    },
  }
}
