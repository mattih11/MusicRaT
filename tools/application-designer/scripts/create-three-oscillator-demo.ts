import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve, join } from 'node:path'
import { compileLaunchApplication } from '../src/control-compiler'
import { compileDeckArtifacts } from '../src/deck-compiler'
import { compileApplication, importApplication, parametersOf, validateWorkspace,
  type ApplicationDocument, type ModuleDescriptor } from '../src/model'
import { addSurfaceWidget, routeSurfaceWidget, updateSurfaceWidget } from '../src/ui-designer-model'
import { uiComponentDefinition } from '../src/ui-component-catalog'

const build = resolve(process.argv[2] ?? '../../build-lvgl-sdl-validation')
const destination = resolve(process.argv[3] ?? '../../build-lvgl-sdl-validation/three-oscillator-live')
const catalog: ModuleDescriptor[] = await Promise.all(
  ['ThreeOscillator', 'AudioDeviceSink'].map(async (name) =>
    JSON.parse(await readFile(join(build, `MusicRaT${name}.module.json`), 'utf8'))))
const descriptor = catalog[0]
const parameters = parametersOf(descriptor).filter((parameter) => parameter.automatable)
if (parameters.length !== 23) throw new Error('Build the live ThreeOscillator descriptor first: expected 23 automatable parameters.')
const document: ApplicationDocument = {
  app_name: 'ThreeOscillatorLive',
  modules: [
    { name: 'ThreeOscillator_1', module_class: descriptor.module_class,
      outputs: [{ system_id: 10, instance_id: 1 }, { system_id: 20, instance_id: 1 }],
      period_ms: 10,
      params: { ...descriptor.params_defaults, master_gain: 0.12,
        oscillator_1_level: 0.7, oscillator_2_level: 0.15, oscillator_3_level: 0.1,
        oscillator_2_semitones: 12, oscillator_3_semitones: -12,
        amplitude_attack_ms: 8, amplitude_decay_ms: 120, amplitude_sustain_level: 0.7,
        amplitude_release_ms: 180, filter_mode: 1, filter_cutoff_hz: 1200,
        filter_resonance: 0.9, filter_attack_ms: 12, filter_decay_ms: 220,
        filter_sustain_level: 0.2, filter_release_ms: 150, filter_env_amount_octaves: 2 } },
    { name: 'AudioDeviceSink_1', module_class: catalog[1].module_class, outputs: [],
      inputs: [{ source_system_id: 10, source_instance_id: 1 }],
      module_address: { system_id: 40, instance_id: 1 },
      params: { device_name: '', sample_rate_hz: descriptor.params_defaults!.sample_rate_hz,
        channel_count: 1, period_frames: Math.round(Number(descriptor.params_defaults!.sample_rate_hz) / 100) } },
  ],
  musicrat_control: { schema_version: 1, devices: [], bindings: [],
    deck: { schema_version: 1, panel: { width_mm: 160, height_mm: 100, thickness_mm: 3 },
      elements: [{ id: 'synth', component_id: 'touch-display-5in', display_name: 'Synth',
        position: { x_mm: 0, y_mm: 0 } }] },
    surfaces: [{ id: 'display-synth', display_name: 'Synth', target: 'lvgl', endpoints: [], widgets: [] }] },
}
const workspace = importApplication(document, catalog)
workspace.modules[0].position = { x: 450, y: 0 }
workspace.modules[1].position = { x: 900, y: 0 }
let control = workspace.source.musicrat_control!
for (const [index, parameter] of parameters.entries()) {
  const column = index % 4
  const row = Math.floor(index / 4)
  const x = 2 + column * 25
  const y = 2 + row * 9
  const label = addSurfaceWidget(control, 'display-synth', uiComponentDefinition('text')!)
  control = updateSurfaceWidget(label.control, 'display-synth', label.widgetId, {
    display_name: `${parameter.group}: ${parameter.display_name}${parameter.unit ? ` (${parameter.unit})` : ''}`,
    layout: { x, y, width: 23, height: 3 },
  })
  const definition = uiComponentDefinition(parameter.kind === 'choice' ? 'choice' : 'slider')!
  const added = addSurfaceWidget(control, 'display-synth', definition)
  control = updateSurfaceWidget(added.control, 'display-synth', added.widgetId, {
    display_name: parameter.display_name,
    layout: { x, y: y + 3, width: 22, height: 4 },
  })
  control = routeSurfaceWidget(control, 'display-synth', added.widgetId,
    { module_id: 'ThreeOscillator_1', parameter_id: parameter.id }, 'bidirectional', definition)
  const binding = control.bindings.find((item) =>
    item.target.parameter_id === parameter.id)!
  binding.pickup = 'immediate'
}
const keyboard = addSurfaceWidget(control, 'display-synth', uiComponentDefinition('keyboard')!)
control = updateSurfaceWidget(keyboard.control, 'display-synth', keyboard.widgetId, {
  display_name: 'Performance keyboard',
  layout: { x: 1, y: 58, width: 98, height: 41 },
  note_targets: [{ module_id: 'ThreeOscillator_1', port_id: 'note_events' }],
})
workspace.source.musicrat_control = control
const problems = validateWorkspace(workspace)
if (problems.length) throw new Error(problems.map((problem) => problem.message).join('\n'))
await mkdir(destination, { recursive: true })
const manifest = join(destination, 'ThreeOscillatorLive.lvgl.json')
const launch = compileLaunchApplication(workspace, { lvglManifestPath: manifest, lvglBackend: 3 })
const sink = launch.modules.find((module) => module.musicrat_generated === 'lvgl_widget_sink')!
sink.params = { ...sink.params, width: 1600, height: 1000 }
await Promise.all([
  writeFile(join(destination, 'ThreeOscillatorLive.musicrat.json'), JSON.stringify(compileApplication(workspace), null, 2) + '\n'),
  writeFile(join(destination, 'ThreeOscillatorLive.json'), JSON.stringify(launch, null, 2) + '\n'),
  writeFile(manifest, JSON.stringify(compileDeckArtifacts(workspace)!.lvgl, null, 2) + '\n'),
])
console.log(`Created ${destination}: 23 live sound controls, state feedback, and note keyboard.`)
