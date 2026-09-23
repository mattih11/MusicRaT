import { mkdtemp, readFile, readdir, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { ProjectStore, ProjectStoreError } from './project-store.mjs'

let directory
let store

beforeEach(async () => {
  directory = await mkdtemp(join(tmpdir(), 'musicrat-projects-'))
  store = new ProjectStore(directory)
})

afterEach(async () => {
  await rm(directory, { recursive: true, force: true })
})

const project = (appName, moduleCount = 0) => ({
  app_name: appName,
  modules: Array.from({ length: moduleCount }, (_, index) => ({
    name: `Module_${index + 1}`,
    module_class: 'MusicRaTTest',
  })),
})

const controlProject = {
  schema_version: 1,
  devices: [{
    id: 'fader-bank',
    display_name: 'Fader Bank',
    kind: 'hardware',
    adapter_module_id: 'Module_1',
    endpoints: [{
      id: 'fader-1',
      display_name: 'Fader 1',
      direction: 'input',
      domain: 'unipolar',
      minimum: 0,
      maximum: 1,
    }],
  }],
  bindings: [{
    id: 'gain',
    source: { owner_id: 'fader-bank', endpoint_id: 'fader-1' },
    target: { module_id: 'Module_1', parameter_id: 1 },
    mode: 'absolute',
    transform: { scale: 1, offset: 0 },
  }],
  surfaces: [],
}

describe('atomic project store', () => {
  it('creates, lists, reads, and replaces a project by revision', async () => {
    const created = await store.write('Studio Rig.json', project('StudioRig'), null)
    expect(created.revision).toMatch(/^[0-9a-f]{64}$/)
    expect((await store.list()).projects).toEqual([{
      name: 'Studio Rig.json',
      revision: created.revision,
      app_name: 'StudioRig',
      module_count: 0,
    }])

    const updated = await store.write(
      'Studio Rig.json',
      project('StudioRig', 1),
      created.revision,
    )
    expect(updated.revision).not.toBe(created.revision)
    expect((await store.read('Studio Rig.json')).document.modules).toHaveLength(1)
    expect(await readFile(join(directory, 'Studio Rig.json'), 'utf8')).toMatch(/\n$/)
    expect((await readdir(directory)).filter((name) => name.startsWith('.'))).toEqual([])
  })

  it('rejects stale concurrent writes without changing the winner', async () => {
    const created = await store.write('Session.json', project('Session'), null)
    const results = await Promise.allSettled([
      store.write('Session.json', project('First'), created.revision),
      store.write('Session.json', project('Second'), created.revision),
    ])

    expect(results.map((result) => result.status).sort()).toEqual(['fulfilled', 'rejected'])
    const rejected = results.find((result) => result.status === 'rejected')
    expect(rejected.reason).toBeInstanceOf(ProjectStoreError)
    expect(rejected.reason.code).toBe('conflict')
    expect((await store.read('Session.json')).document.app_name).toBe('First')
  })

  it('rejects traversal, malformed documents, and blind overwrites', async () => {
    expect(() => store.filename('../outside.json')).toThrow(ProjectStoreError)
    await expect(store.write('Broken.json', { app_name: 'Broken' }, null))
      .rejects.toMatchObject({ code: 'invalid_document' })
    await store.write('Existing.json', project('Existing'), null)
    await expect(store.write('Existing.json', project('Replacement'), null))
      .rejects.toMatchObject({ code: 'conflict' })
  })

  it('round-trips valid control metadata and rejects unresolved references', async () => {
    const document = { ...project('ControlRig', 1), musicrat_control: controlProject }
    const created = await store.write('ControlRig.json', document, null)
    expect((await store.read('ControlRig.json')).document.musicrat_control)
      .toEqual(controlProject)

    const invalidControl = structuredClone(controlProject)
    invalidControl.bindings[0].source.endpoint_id = 'missing'
    await expect(store.write(
      'InvalidControl.json',
      { ...project('InvalidControl', 1), musicrat_control: invalidControl },
      null,
    )).rejects.toMatchObject({ code: 'invalid_document' })
    expect(created.document.musicrat_control.bindings[0].id).toBe('gain')
  })
})