import { describe, expect, it } from 'vitest'
import { demoApplication } from './demo'
import { uiComponentDefinition } from './ui-component-catalog'
import { addSurfaceWidget, clampWidgetLayout, removeSurfaceWidget, routeSurfaceWidget } from './ui-designer-model'

function project() {
  const control = structuredClone(demoApplication.musicrat_control!)
  control.surfaces.push({
    id: 'display-main',
    display_name: 'Main display',
    target: 'lvgl',
    endpoints: [],
    widgets: [],
  })
  return control
}

describe('UI designer model', () => {
  it('adds an interactive widget and derives its surface endpoint', () => {
    const { control, widgetId } = addSurfaceWidget(project(), 'display-main', uiComponentDefinition('knob')!)
    const surface = control.surfaces.find((item) => item.id === 'display-main')!

    expect(surface.widgets[0]).toMatchObject({
      id: widgetId,
      kind: 'knob',
      route: 'bidirectional',
      layout: { x: 5, y: 5, width: 18, height: 24 },
      endpoint_id: widgetId,
    })
    expect(surface.endpoints[0]).toMatchObject({
      id: widgetId,
      direction: 'bidirectional',
      domain: 'unipolar',
    })
  })

  it('creates control and feedback bindings transactionally', () => {
    const created = addSurfaceWidget(project(), 'display-main', uiComponentDefinition('fader')!)
    const control = routeSurfaceWidget(
      created.control,
      'display-main',
      created.widgetId,
      { module_id: 'Instrument_1', parameter_id: 1 },
      'bidirectional',
      uiComponentDefinition('fader')!,
    )

    const widget = control.surfaces.find((item) => item.id === 'display-main')!.widgets[0]
    expect(control.bindings[0]).toMatchObject({
      id: widget.binding_id,
      source: { owner_id: 'display-main', endpoint_id: widget.id },
      target: { module_id: 'Instrument_1', parameter_id: 1 },
      feedback: { owner_id: 'display-main', endpoint_id: widget.id },
    })

    const removed = removeSurfaceWidget(control, 'display-main', widget.id)
    const surface = removed.surfaces.find((item) => item.id === 'display-main')!
    expect(surface.widgets).toEqual([])
    expect(surface.endpoints).toEqual([])
    expect(removed.bindings).toEqual([])
  })

  it('supports feedback-only widgets without fake control bindings', () => {
    const created = addSurfaceWidget(project(), 'display-main', uiComponentDefinition('meter')!)
    const control = routeSurfaceWidget(
      created.control,
      'display-main',
      created.widgetId,
      { module_id: 'Instrument_1', parameter_id: 1 },
      'feedback',
      uiComponentDefinition('meter')!,
    )

    const surface = control.surfaces.find((item) => item.id === 'display-main')!
    expect(surface.widgets[0].parameter).toEqual({ module_id: 'Instrument_1', parameter_id: 1 })
    expect(surface.endpoints).toEqual([])
    expect(control.bindings).toEqual([])
  })

  it('snaps and clamps widget geometry to its display', () => {
    expect(clampWidgetLayout({ x: 97, y: -2, width: 18, height: 24 }, 5)).toEqual({
      x: 80,
      y: 0,
      width: 20,
      height: 25,
    })
  })
})
