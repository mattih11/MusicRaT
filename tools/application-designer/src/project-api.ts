import type { ApplicationDocument } from './model'

export interface ProjectSummary {
  name: string
  revision: string
  app_name: string
  module_count: number
}

export interface HostedProject {
  name: string
  revision: string
  document: ApplicationDocument
}

interface ProjectList {
  projects: ProjectSummary[]
  warnings: string[]
}

async function responseJson<T>(response: Response): Promise<T> {
  const value = await response.json() as T & { error?: string }
  if (!response.ok) throw new Error(value.error ?? `Project service returned ${response.status}.`)
  return value
}

export function projectFilename(appName: string): string {
  const normalized = appName
    .trim()
    .replace(/[^A-Za-z0-9._ -]+/g, '-')
    .replace(/^[^A-Za-z0-9]+/, '')
    .slice(0, 122)
  return `${normalized || 'musicrat-application'}.json`
}

export async function listHostedProjects(signal?: AbortSignal): Promise<ProjectList> {
  return responseJson(await fetch('/api/projects', { signal }))
}

export async function loadHostedProject(name: string): Promise<HostedProject> {
  return responseJson(await fetch(`/api/projects/${encodeURIComponent(name)}`))
}

export async function saveHostedProject(
  name: string,
  document: ApplicationDocument,
  expectedRevision: string | null,
): Promise<HostedProject> {
  return responseJson(await fetch(`/api/projects/${encodeURIComponent(name)}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ document, expected_revision: expectedRevision }),
  }))
}