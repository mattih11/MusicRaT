import { Handle, Position, type NodeProps } from '@xyflow/react'
import { CircleGauge, Cpu, Monitor } from 'lucide-react'
import type { BindingCanvasNode } from './binding-canvas-model'

export function BindingOwnerNodeView({ data, selected }: NodeProps<BindingCanvasNode>) {
  if (data.kind !== 'owner') return null
  const Icon = data.owner_kind === 'hardware' ? Cpu : Monitor
  return <article className={`binding-node binding-owner-node${selected ? ' is-selected' : ''}`}>
    <header>
      <Icon size={15} aria-hidden="true" />
      <span><strong>{data.display_name}</strong><small>{data.owner_kind}</small></span>
    </header>
    <div className="binding-node-rows">
      {data.endpoints.map((endpoint) => <div className="binding-node-row" key={endpoint.id}>
        <span><strong>{endpoint.display_name}</strong><small>{endpoint.domain}</small></span>
        <CircleGauge size={12} aria-hidden="true" />
        <Handle type="source" position={Position.Right} id={endpoint.id}
          title={`${endpoint.display_name} (${endpoint.domain})`} />
      </div>)}
    </div>
  </article>
}

export function BindingTargetNodeView({ data, selected }: NodeProps<BindingCanvasNode>) {
  if (data.kind !== 'target') return null
  return <article className={`binding-node binding-target-node${selected ? ' is-selected' : ''}`}>
    <header>
      <span><strong>{data.display_name}</strong>
        <small>{data.module_class.replace('MusicRaT', '')}</small></span>
    </header>
    <div className="binding-node-rows">
      {data.parameters.map((parameter) => <div className="binding-node-row target-row" key={parameter.id}>
        <Handle type="target" position={Position.Left} id={String(parameter.id)}
          title={`${parameter.display_name} (${parameter.unit || parameter.kind})`} />
        <CircleGauge size={12} aria-hidden="true" />
        <span><strong>{parameter.display_name}</strong>
          <small>{parameter.minimum}–{parameter.maximum} {parameter.unit}</small></span>
      </div>)}
    </div>
  </article>
}
