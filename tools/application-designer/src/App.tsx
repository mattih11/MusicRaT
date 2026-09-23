import { useEffect, useRef, useState } from 'react'
import {
  Background,
  BackgroundVariant,
  Controls,
  MiniMap,
  ReactFlow,
  addEdge,
  applyEdgeChanges,
  applyNodeChanges,
  type Connection,
  type Edge,
  type EdgeChange,
  type NodeChange,
} from '@xyflow/react'
import '@xyflow/react/dist/style.css'
import { Download, FileJson, FolderOpen, Plus, Save, Search, Upload, Waves } from 'lucide-react'
import './App.css'
import { compileLaunchApplication } from './control-compiler'
import { demoApplication, demoDescriptors } from './demo'
import { ModuleNodeView, type ModuleNode } from './ModuleNode'
import {
  compileApplication,
  importApplication,
  parametersOf,
  portsOf,
  validateConnection,
  validateWorkspace,
  type ApplicationDocument,
  type DesignerConnection,
  type DesignerWorkspace,
  type ModuleDescriptor,
} from './model'
import {
  listHostedProjects,
  loadHostedProject,
  projectFilename,
  saveHostedProject,
  type ProjectSummary,
} from './project-api'

const nodeTypes = { musicratModule: ModuleNodeView }

function workspaceToNodes(workspace: DesignerWorkspace): ModuleNode[] {
  return workspace.modules.map((module) => ({
    id: module.id,
    type: 'musicratModule',
    position: module.position,
    data: module,
  }))
}

function workspaceToEdges(workspace: DesignerWorkspace): Edge[] {
  return workspace.connections.map((connection) => ({
    id: connection.id,
    source: connection.source_module_id,
    sourceHandle: connection.source_port_id,
    target: connection.target_module_id,
    targetHandle: connection.target_port_id,
    className: `edge-${connection.source_port_id.includes('note') ? 'note' : 'signal'}`,
  }))
}

function edgeToConnection(edge: Edge): DesignerConnection {
  return {
    id: edge.id,
    source_module_id: edge.source,
    source_port_id: edge.sourceHandle ?? '',
    target_module_id: edge.target,
    target_port_id: edge.targetHandle ?? '',
  }
}

function currentWorkspace(
  appName: string,
  nodes: ModuleNode[],
  edges: Edge[],
  source: ApplicationDocument,
): DesignerWorkspace {
  return {
    appName,
    modules: nodes.map((node) => ({ ...node.data, position: node.position })),
    connections: edges.map(edgeToConnection),
    source,
  }
}

function downloadJson(name: string, value: unknown) {
  const blob = new Blob([`${JSON.stringify(value, null, 2)}\n`], { type: 'application/json' })
  const link = document.createElement('a')
  link.href = URL.createObjectURL(blob)
  link.download = name
  link.click()
  URL.revokeObjectURL(link.href)
}

async function readJsonFiles(files: FileList): Promise<unknown[]> {
  return Promise.all(Array.from(files).map(async (file) => JSON.parse(await file.text()) as unknown))
}

function App() {
  const initialWorkspace = importApplication(demoApplication, demoDescriptors)
  const [catalog, setCatalog] = useState(demoDescriptors)
  const [appName, setAppName] = useState(initialWorkspace.appName)
  const [sourceDocument, setSourceDocument] = useState(initialWorkspace.source)
  const [nodes, setNodes] = useState<ModuleNode[]>(workspaceToNodes(initialWorkspace))
  const [edges, setEdges] = useState<Edge[]>(workspaceToEdges(initialWorkspace))
  const [selectedId, setSelectedId] = useState<string | null>(null)
  const [query, setQuery] = useState('')
  const [message, setMessage] = useState('Demo graph is valid and ready to export.')
  const [catalogSource, setCatalogSource] = useState<'demo' | 'installed'>('demo')
  const [projects, setProjects] = useState<ProjectSummary[]>([])
  const [selectedProjectName, setSelectedProjectName] = useState('')
  const [hostedProject, setHostedProject] = useState<{ name: string; revision: string } | null>(null)
  const [saving, setSaving] = useState(false)
  const descriptorInput = useRef<HTMLInputElement>(null)
  const projectInput = useRef<HTMLInputElement>(null)

  const workspace = currentWorkspace(appName, nodes, edges, sourceDocument)
  const issues = validateWorkspace(workspace)
  const selected = nodes.find((node) => node.id === selectedId)

  useEffect(() => {
    const controller = new AbortController()
    fetch('/api/catalog', { signal: controller.signal })
      .then(async (response) => {
        if (!response.ok) throw new Error(`Catalog service returned ${response.status}.`)
        return response.json() as Promise<{ descriptors: ModuleDescriptor[]; warnings: string[] }>
      })
      .then((result) => {
        if (result.descriptors.length === 0) {
          setMessage('No installed descriptors found; using the demo catalog.')
          return
        }
        setCatalog(result.descriptors)
        setCatalogSource('installed')
        setAppName('UntitledMusicRaTApplication')
        setSourceDocument({ app_name: 'UntitledMusicRaTApplication', modules: [] })
        setNodes([])
        setEdges([])
        setSelectedId(null)
        setHostedProject(null)
        setSelectedProjectName('')
        setMessage(`Loaded ${result.descriptors.length} installed module descriptors${result.warnings.length ? ` with ${result.warnings.length} warnings` : ''}.`)
      })
      .catch((error: unknown) => {
        if (error instanceof DOMException && error.name === 'AbortError') return
        setMessage('Catalog host unavailable; using the demo catalog.')
      })
    return () => controller.abort()
  }, [])

  useEffect(() => {
    const controller = new AbortController()
    listHostedProjects(controller.signal)
      .then((result) => setProjects(result.projects))
      .catch(() => {})
    return () => controller.abort()
  }, [])

  const onNodesChange = (changes: NodeChange<ModuleNode>[]) => setNodes((items) => applyNodeChanges(changes, items))
  const onEdgesChange = (changes: EdgeChange[]) => setEdges((items) => applyEdgeChanges(changes, items))
  const onConnect = (connection: Connection) => {
    if (!connection.source || !connection.target || !connection.sourceHandle || !connection.targetHandle) return
    const candidate: DesignerConnection = {
      id: `${connection.source}:${connection.sourceHandle}->${connection.target}:${connection.targetHandle}`,
      source_module_id: connection.source,
      source_port_id: connection.sourceHandle,
      target_module_id: connection.target,
      target_port_id: connection.targetHandle,
    }
    const problem = validateConnection(workspace.modules, candidate, workspace.connections)[0]
    if (problem) {
      setMessage(problem.message)
      return
    }
    setEdges((items) => addEdge({ ...connection, id: candidate.id }, items))
    setMessage('Connection added.')
  }

  const addModule = (descriptor: ModuleDescriptor) => {
    const stem = descriptor.module_class.replace('MusicRaT', '') || 'Module'
    let suffix = 1
    while (nodes.some((node) => node.id === `${stem}_${suffix}`)) suffix += 1
    const id = `${stem}_${suffix}`
    setNodes((items) => [...items, {
      id,
      type: 'musicratModule',
      position: { x: 120 + items.length * 36, y: 100 + items.length * 28 },
      data: {
        id,
        descriptor,
        position: { x: 0, y: 0 },
        source: { name: id, module_class: descriptor.module_class, params: structuredClone(descriptor.params_defaults ?? {}) },
      },
    }])
  }

  const loadDescriptors = async (files: FileList | null) => {
    if (!files?.length) return
    try {
      const loaded = (await readJsonFiles(files)) as ModuleDescriptor[]
      const usable = loaded.filter((item) => item.module_class)
      setCatalog(usable)
      setCatalogSource('installed')
      setMessage(`Loaded ${usable.length} module descriptors from files.`)
    } catch (error) {
      setMessage(error instanceof Error ? error.message : 'Could not read descriptors.')
    }
  }

  const loadProject = async (files: FileList | null) => {
    if (!files?.length) return
    try {
      const [application] = await readJsonFiles(files) as ApplicationDocument[]
      const loaded = importApplication(application, catalog)
      setAppName(loaded.appName)
      setSourceDocument(loaded.source)
      setNodes(workspaceToNodes(loaded))
      setEdges(workspaceToEdges(loaded))
      setHostedProject(null)
      setSelectedProjectName('')
      setMessage(`Opened ${application.app_name}.`)
    } catch (error) {
      setMessage(error instanceof Error ? error.message : 'Could not read application.')
    }
  }

  const openHostedProject = async () => {
    if (!selectedProjectName) return
    try {
      const project = await loadHostedProject(selectedProjectName)
      const loaded = importApplication(project.document, catalog)
      setAppName(loaded.appName)
      setSourceDocument(loaded.source)
      setNodes(workspaceToNodes(loaded))
      setEdges(workspaceToEdges(loaded))
      setSelectedId(null)
      setHostedProject({ name: project.name, revision: project.revision })
      setMessage(`Opened ${project.name}.`)
    } catch (error) {
      setMessage(error instanceof Error ? error.message : 'Could not open hosted project.')
    }
  }

  const saveProject = async () => {
    try {
      const document = compileApplication(workspace, { validate: false })
      const name = hostedProject?.name ?? projectFilename(document.app_name)
      setSaving(true)
      const saved = await saveHostedProject(
        name,
        document,
        hostedProject?.revision ?? null,
      )
      setHostedProject({ name: saved.name, revision: saved.revision })
      setSelectedProjectName(saved.name)
      const result = await listHostedProjects()
      setProjects(result.projects)
      setMessage(`Saved ${saved.name}.`)
    } catch (error) {
      setMessage(error instanceof Error ? error.message : 'Could not save project.')
    } finally {
      setSaving(false)
    }
  }

  const exportProject = () => {
    try {
      downloadJson(
        `${appName || 'musicrat-application'}.json`,
        compileLaunchApplication(workspace),
      )
      setMessage('CommRaT application JSON exported.')
    } catch (error) {
      setMessage(error instanceof Error ? error.message : 'Application is invalid.')
    }
  }

  const updateParameter = (name: string, value: unknown) => {
    setNodes((items) => items.map((node) => node.id === selectedId ? {
      ...node,
      data: {
        ...node.data,
        source: { ...node.data.source, params: { ...node.data.source.params, [name]: value } },
      },
    } : node))
  }

  return (
    <main className="designer-shell">
      <header className="topbar">
        <div className="brand"><Waves size={22} /><span>MusicRaT</span><strong>Application Designer</strong></div>
        <label className="app-name"><span>Application</span><input value={appName} onChange={(event) => setAppName(event.target.value)} /></label>
        <div className="toolbar">
          <input ref={descriptorInput} hidden type="file" accept="application/json,.json" multiple onChange={(event) => void loadDescriptors(event.target.files)} />
          <input ref={projectInput} hidden type="file" accept="application/json,.json" onChange={(event) => void loadProject(event.target.files)} />
          <select className="project-picker" aria-label="Saved projects" value={selectedProjectName} onChange={(event) => setSelectedProjectName(event.target.value)}>
            <option value="">Saved projects</option>
            {projects.map((project) => <option key={project.name} value={project.name}>{project.app_name}</option>)}
          </select>
          <button className="icon-button compact-button" title="Open saved project" aria-label="Open saved project" disabled={!selectedProjectName} onClick={() => void openHostedProject()}><FolderOpen size={17} /></button>
          <button className="icon-button compact-button" title="Load module descriptors" aria-label="Load module descriptors" onClick={() => descriptorInput.current?.click()}><Upload size={17} /></button>
          <button className="icon-button compact-button" title="Import application JSON" aria-label="Import application JSON" onClick={() => projectInput.current?.click()}><FolderOpen size={17} /></button>
          <button className="primary-button" title="Save project" aria-label="Save project" disabled={saving} onClick={() => void saveProject()}><Save size={17} /><span>{saving ? 'Saving' : 'Save'}</span></button>
          <button className="icon-button compact-button" title="Download application JSON" aria-label="Download application JSON" onClick={exportProject}><Download size={17} /></button>
        </div>
      </header>

      <aside className="catalog-panel">
        <div className="panel-heading"><div><span className="eyebrow">Catalog · {catalogSource}</span><h2>Modules</h2></div><span className="count">{catalog.length}</span></div>
        <label className="search"><Search size={15} /><input aria-label="Search modules" placeholder="Filter modules" value={query} onChange={(event) => setQuery(event.target.value)} /></label>
        <div className="catalog-list">
          {catalog.filter((item) => item.module_class.toLowerCase().includes(query.toLowerCase())).map((descriptor) => (
            <button className="catalog-item" key={descriptor.module_class} onClick={() => addModule(descriptor)}>
              <span><strong>{descriptor.module_class.replace('MusicRaT', '')}</strong><small>{portsOf(descriptor).length} ports · {parametersOf(descriptor).length} parameters</small></span>
              <Plus size={16} aria-hidden="true" />
            </button>
          ))}
        </div>
        <div className="domain-key"><span className="audio-dot" />Audio <span className="note-dot" />Notes <span className="parameter-dot" />Parameters</div>
      </aside>

      <section className="canvas" aria-label="Application graph">
        <ReactFlow<ModuleNode>
          key={catalogSource}
          nodes={nodes}
          edges={edges}
          nodeTypes={nodeTypes}
          onNodesChange={onNodesChange}
          onEdgesChange={onEdgesChange}
          onConnect={onConnect}
          onNodeClick={(_, node) => setSelectedId(node.id)}
          onPaneClick={() => setSelectedId(null)}
          fitView
          minZoom={0.35}
          maxZoom={1.8}
        >
          <Background variant={BackgroundVariant.Dots} gap={22} size={1} color="#cad0d0" />
          <Controls showInteractive={false} />
          <MiniMap pannable zoomable nodeColor="#254c46" maskColor="rgba(235, 238, 234, 0.78)" />
        </ReactFlow>
      </section>

      <aside className="inspector-panel">
        <div className="panel-heading"><div><span className="eyebrow">Inspector</span><h2>{selected?.id ?? 'Application'}</h2></div><FileJson size={19} /></div>
        {selected ? (
          <div className="inspector-content">
            <dl><dt>Class</dt><dd>{selected.data.descriptor.module_class}</dd><dt>Execution</dt><dd>{selected.data.descriptor.execution_mode ?? 'unspecified'}</dd></dl>
            <h3>Parameters</h3>
            {parametersOf(selected.data.descriptor).length === 0 && <p className="empty-state">No published parameter metadata.</p>}
            {parametersOf(selected.data.descriptor).map((parameter) => {
              const value = selected.data.source.params?.[parameter.name] ?? ''
              return <label className="parameter-field" key={parameter.id}><span>{parameter.display_name}<small>{parameter.unit}</small></span>
                {parameter.kind === 'boolean'
                  ? <input type="checkbox" checked={Boolean(value)} onChange={(event) => updateParameter(parameter.name, event.target.checked)} />
                  : parameter.kind === 'choice'
                    ? <select value={String(value)} onChange={(event) => updateParameter(parameter.name, Number(event.target.value))}>{parameter.choices.map((choice) => <option key={choice.value} value={choice.value}>{choice.label}</option>)}</select>
                    : parameter.kind === 'text'
                      ? <input type="text" value={String(value)} onChange={(event) => updateParameter(parameter.name, event.target.value)} />
                      : <input type="number" value={String(value)} min={parameter.minimum} max={parameter.maximum} step={parameter.step || 'any'} onChange={(event) => updateParameter(parameter.name, Number(event.target.value))} />}
              </label>
            })}
          </div>
        ) : (
          <div className="inspector-content overview"><strong>{nodes.length} modules · {edges.length} routes</strong><p>Select a module to edit its startup parameters.</p></div>
        )}
      </aside>

      <footer className={`statusbar ${issues.length ? 'has-errors' : ''}`}>
        <span className="status-indicator" />
        <strong>{issues.length ? `${issues.length} validation issue${issues.length === 1 ? '' : 's'}` : 'Ready'}</strong>
        <span>{issues[0]?.message ?? message}</span>
      </footer>
    </main>
  )
}

export default App
