import { Handle, Position, type NodeProps } from '@xyflow/react'
import { Music2 } from 'lucide-react'
import type { KeyboardNode } from './keyboard-graph-model'

export function KeyboardNodeView({ data, selected }: NodeProps<KeyboardNode>) {
  return <article className={`module-node${selected ? ' is-selected' : ''}`}>
    <header>
      <span className="module-kind">LVGL keyboard</span>
      <strong>{data.display_name}</strong>
      <small>{data.surface_name}</small>
    </header>
    <div className="port-grid" style={{ '--port-rows': 1 } as React.CSSProperties}>
      <div className="port-column input-ports" />
      <div className="port-column output-ports">
        <div className="port port-note">
          <span>Note events</span>
          <Music2 size={12} aria-hidden="true" />
          <Handle type="source" position={Position.Right} id="note_events"
            title="Note events (note)" />
        </div>
      </div>
    </div>
  </article>
}
