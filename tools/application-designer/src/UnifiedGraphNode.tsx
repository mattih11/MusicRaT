import { Handle, Position, useUpdateNodeInternals, type NodeProps } from '@xyflow/react'
import { useEffect } from 'react'
import { Cpu, Monitor, SlidersHorizontal } from 'lucide-react'
import type { GraphNode, GraphPort } from './unified-graph-model'

function Port({ port, direction }: { port: GraphPort; direction: 'source' | 'target' }) {
  return <div className={`graph-port port-${port.domain}`}>
    <Handle type={direction} position={direction === 'source' ? Position.Right : Position.Left}
      id={port.id} title={`${port.label} (${port.domain})`} />
    <span>{port.label}</span>
    <small>{port.domain}</small>
  </div>
}

export function UnifiedGraphNode({ id, data, selected }: NodeProps<GraphNode>) {
  const updateNodeInternals = useUpdateNodeInternals()
  const handles = [...data.inputs, ...data.outputs].map((port) => port.id).join('\n')
  useEffect(() => updateNodeInternals(id), [id, handles, updateNodeInternals])
  const Icon = data.kind === 'hardware' ? Cpu : data.kind === 'ui' ? Monitor : SlidersHorizontal
  return <article className={`unified-node graph-${data.kind}${selected ? ' is-selected' : ''}`}>
    <header>
      <Icon size={15} aria-hidden="true" />
      <div><small>{data.generated ? 'Generated · read-only' : data.kind}</small>
        <strong>{data.title}</strong><span>{data.subtitle}</span></div>
    </header>
    <div className="graph-ports">
      <div>{data.inputs.map((port) => <Port key={port.id} port={port} direction="target" />)}</div>
      <div>{data.outputs.map((port) => <Port key={port.id} port={port} direction="source" />)}</div>
    </div>
    {Boolean(data.startup_parameters) && <footer>{data.startup_parameters} startup-only settings in Inspector</footer>}
  </article>
}
