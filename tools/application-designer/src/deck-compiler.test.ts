import { describe, expect, it } from 'vitest'
import { compileDeckArtifacts } from './deck-compiler'
import { componentDefinition } from './component-catalog'
import { createDefaultDeck, createDefaultDeckElement, deriveDeckControlProject } from './deck-editor-model'
import { demoApplication, demoDescriptors } from './demo'
import { importApplication } from './model'
import { uiComponentDefinition } from './ui-component-catalog'
import { addSurfaceWidget, observeSurfaceWidget, routeSurfaceWidget } from './ui-designer-model'

function workspace() {
  const application = structuredClone(demoApplication)
  const deck = createDefaultDeck()
  deck.elements.push(createDefaultDeckElement(deck, componentDefinition('fader-60mm')!))
  deck.elements.push(createDefaultDeckElement(deck, componentDefinition('touch-display-5in')!))
  application.musicrat_control = deriveDeckControlProject(application.musicrat_control!, deck)
  return importApplication(application, demoDescriptors)
}

describe('deck artifact compiler', () => {
  it('emits a logical manifest and leaves electrical allocation to the driver', () => {
    const artifacts = compileDeckArtifacts(workspace())!

    expect(artifacts.config_fingerprint).toMatch(/^fnv1a32:[0-9a-f]{8}$/)
    expect(artifacts.driver).toEqual({
      schema_version: 1,
      config_fingerprint: artifacts.config_fingerprint,
      device_id: 'deck-hardware',
      panel: { width_mm: 320, height_mm: 140, thickness_mm: 3 },
      components: [{
        instance_id: 'fader-1',
        component_id: 'fader-60mm',
        driver_class: 'absolute-analog',
        endpoint_id: 'fader-1',
        surface_id: undefined,
      }, {
        instance_id: 'touch-display-1',
        component_id: 'touch-display-5in',
        driver_class: 'lvgl-touch-display',
        endpoint_id: undefined,
        surface_id: 'display-touch-display-1',
      }],
    })
    expect(JSON.stringify(artifacts)).not.toMatch(/gpio|spi|uart|adc|pwm/i)
  })

  it('is deterministic across declaration order', () => {
    const first = workspace()
    const reordered = workspace()
    reordered.source.musicrat_control!.deck!.elements.reverse()
    expect(compileDeckArtifacts(reordered)).toEqual(compileDeckArtifacts(first))
  })

  it('preserves UI layout and parameter routes in the LVGL manifest', () => {
    const source = workspace()
    const control = source.source.musicrat_control!
    const surfaceId = 'display-touch-display-1'
    const created = addSurfaceWidget(control, surfaceId, uiComponentDefinition('knob')!)
    source.source.musicrat_control = routeSurfaceWidget(
      created.control,
      surfaceId,
      created.widgetId,
      { module_id: 'Instrument_1', parameter_id: 1 },
      'bidirectional',
      uiComponentDefinition('knob')!,
    )

    expect(compileDeckArtifacts(source)!.lvgl.displays[0].widgets[0]).toMatchObject({
      kind: 'knob',
      layout: { x: 5, y: 5, width: 18, height: 24 },
      route: 'bidirectional',
      parameter: { module_id: 'Instrument_1', parameter_id: 1 },
    })
  })

  it('exports routed parameter choices for LVGL choice widgets', () => {
    const source = workspace()
    const parameter = source.modules.find((module) => module.id === 'Instrument_1')!
      .descriptor.descriptor_metadata!.musicrat_parameters!.parameters[0]
    parameter.kind = 'choice'
    parameter.choices = [{ value: 0, label: 'Clean' }, { value: 1, label: 'Drive' }]
    const control = source.source.musicrat_control!
    const surfaceId = 'display-touch-display-1'
    const created = addSurfaceWidget(control, surfaceId, uiComponentDefinition('choice')!)
    source.source.musicrat_control = routeSurfaceWidget(
      created.control,
      surfaceId,
      created.widgetId,
      { module_id: 'Instrument_1', parameter_id: 1 },
      'bidirectional',
      uiComponentDefinition('choice')!,
    )

    expect(compileDeckArtifacts(source)!.lvgl.displays[0].widgets[0]).toMatchObject({
      kind: 'choice',
      choices: [{ value: 0, label: 'Clean' }, { value: 1, label: 'Drive' }],
    })
  })

  it('preserves observable routes in the LVGL manifest', () => {
    const source = workspace()
    const control = source.source.musicrat_control!
    const surfaceId = 'display-touch-display-1'
    const created = addSurfaceWidget(control, surfaceId, uiComponentDefinition('meter')!)
    source.modules.push({
      id: 'Meter_1',
      descriptor: {
        module_class: 'MusicRaTLevelMeter',
        binary: '',
        outputs: ['CommRaT::Messages::LevelMeterBlock'],
        descriptor_metadata: {
          musicrat_ports: { schema_version: 1, ports: [{
            id: 'levels', display_name: 'Levels', direction: 'output',
            port_index: 0, domain: 'telemetry', required: true, max_connections: 1,
          }] },
          musicrat_observables: { schema_version: 1, observables: [{
            id: 'peak', display_name: 'Peak', group: 'Levels', output_port_id: 'levels',
            selector: 'channels[].peak', kind: 'continuous', unit: 'linear',
            minimum: 0, maximum: 1, channel_selectable: true,
          }] },
        },
      },
      source: { name: 'Meter_1', module_class: 'MusicRaTLevelMeter' },
      position: { x: 0, y: 0 },
    })
    source.source.musicrat_control = observeSurfaceWidget(
      created.control,
      surfaceId,
      created.widgetId,
      { module_id: 'Meter_1', observable_id: 'peak', channel: 0 },
    )

    const lvgl = compileDeckArtifacts(source)!.lvgl
    expect(lvgl.observation_bindings).toEqual([{
      id: 'observation-1',
      source: { module_id: 'Meter_1', observable_id: 'peak', channel: 0 },
      target: { surface_id: surfaceId, widget_id: created.widgetId, property: 'value' },
    }])
    expect(lvgl.displays[0].widgets[0]).toMatchObject({
      observable: { module_id: 'Meter_1', observable_id: 'peak', channel: 0 },
      observation_binding_id: 'observation-1',
    })
  })
})
