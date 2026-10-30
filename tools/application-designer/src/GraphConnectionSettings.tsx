import type { DesignerWorkspace } from './model'
import type { GraphRoute } from './unified-graph-model'

export function GraphConnectionSettings({ workspace, route, onChange }: {
  workspace: DesignerWorkspace
  route?: GraphRoute
  onChange: (workspace: DesignerWorkspace) => void
}) {
  const control = workspace.source.musicrat_control
  if (!route || !control) return null
  if (route.kind === 'action') {
    const binding = control.action_bindings?.find((item) => item.id === route.id)
    if (!binding) return null
    const update = (change: Partial<typeof binding>) => onChange({
      ...workspace, source: { ...workspace.source, musicrat_control: {
        ...control, action_bindings: control.action_bindings!.map((item) =>
          item.id === binding.id ? { ...item, ...change } : item),
      } },
    })
    return <div className="binding-form">
      <p>{binding.source.endpoint_id} → {binding.target.module_id} / {binding.target.action_id}</p>
      <label><span>Quantization</span><select aria-label="Action quantization" value={binding.quantization}
        onChange={(event) => {
          const quantization = event.target.value
          if (quantization === 'immediate' || quantization === 'beat' || quantization === 'bar') update({ quantization })
        }}>
        <option value="immediate">Immediate</option><option value="beat">Beat</option><option value="bar">Bar</option>
      </select></label>
      <label><span>Ramp frames</span><input aria-label="Action ramp frames" type="number"
        min="0" step="1" value={binding.ramp_frames ?? 0}
        onChange={(event) => update({ ramp_frames: Number(event.target.value) })} /></label>
    </div>
  }
  if (route.kind === 'observation') {
    const binding = control.observation_bindings?.find((item) => item.id === route.id)
    return binding && <p>{binding.source.module_id} / {binding.source.observable_id}
      {binding.source.channel === undefined ? '' : ` / Ch ${binding.source.channel + 1}`}
      {' → '}{binding.target.widget_id} ({binding.target.property})</p>
  }
  return null
}
