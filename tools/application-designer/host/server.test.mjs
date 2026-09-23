import { mkdtemp, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { ProjectStore } from './project-store.mjs'
import { createDesignerServer } from './server.mjs'

let directory
let server
let origin

beforeEach(async () => {
  directory = await mkdtemp(join(tmpdir(), 'musicrat-server-'))
  server = createDesignerServer({ projectStore: new ProjectStore(directory) })
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
  const address = server.address()
  origin = `http://127.0.0.1:${address.port}`
})

afterEach(async () => {
  await new Promise((resolve) => server.close(resolve))
  await rm(directory, { recursive: true, force: true })
})

async function putProject(name, document, expectedRevision) {
  return fetch(`${origin}/api/projects/${encodeURIComponent(name)}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ document, expected_revision: expectedRevision }),
  })
}

describe('project API', () => {
  it('creates, lists, loads, and updates a project', async () => {
    const document = { app_name: 'StudioRig', modules: [] }
    const createResponse = await putProject('StudioRig.json', document, null)
    expect(createResponse.status).toBe(200)
    const created = await createResponse.json()

    const listResponse = await fetch(`${origin}/api/projects`)
    expect(await listResponse.json()).toMatchObject({
      projects: [{ name: 'StudioRig.json', app_name: 'StudioRig', module_count: 0 }],
      warnings: [],
    })

    const readResponse = await fetch(`${origin}/api/projects/StudioRig.json`)
    expect(await readResponse.json()).toEqual(created)

    const updateResponse = await putProject(
      'StudioRig.json',
      { ...document, modules: [{ name: 'Oscillator_1', module_class: 'MusicRaTSineOscillator' }] },
      created.revision,
    )
    expect(updateResponse.status).toBe(200)
    expect((await updateResponse.json()).revision).not.toBe(created.revision)
  })

  it('reports conflicts and invalid requests', async () => {
    const document = { app_name: 'Session', modules: [] }
    await putProject('Session.json', document, null)

    const conflict = await putProject('Session.json', document, null)
    expect(conflict.status).toBe(409)
    expect(await conflict.json()).toMatchObject({ code: 'conflict' })

    const invalidName = await putProject('../outside.json', document, null)
    expect(invalidName.status).toBe(400)
    expect(await invalidName.json()).toMatchObject({ code: 'invalid_name' })

    const missing = await fetch(`${origin}/api/projects/Missing.json`)
    expect(missing.status).toBe(404)
  })
})