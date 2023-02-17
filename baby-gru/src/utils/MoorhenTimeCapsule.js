import localforage from 'localforage';

const createInstance = (name, empty=false) => {
    console.log(`Creating local storage instance for time capsule...`)
    const instance = localforage.createInstance({
        driver: [localforage.INDEXEDDB, localforage.LOCALSTORAGE],
        name: name,
        storeName: name
     })
     if (empty) {
        instance.clear()
     }
     return instance
}

export function MoorhenTimeCapsule(moleculesRef, glRef, preferences) {
    this.autoBackupsStorageInstance = createInstance('Moorhen-TimeCapsule-Auto', true)
    this.manualBackupsStorageInstance = createInstance('Moorhen-TimeCapsule-Manual')
    this.moleculesRef = moleculesRef
    this.glRef = glRef
    this.preferences = preferences
    this.busy = false
    this.modificationCount = 0
    this.modificationCountBackupThreshold = 5
    this.maxBackupCount = 10
}

MoorhenTimeCapsule.prototype.fetchSession = async function () {
    let moleculePromises = this.moleculesRef.current.map(molecule => {return molecule.getAtoms()})
    let moleculeAtoms = await Promise.all(moleculePromises)

    const session = {
        moleculesNames: this.moleculesRef.current.map(molecule => molecule.name),
        mapsNames: [],
        moleculesPdbData: moleculeAtoms.map(item => item.data.result.pdbData),
        mapsMapData: [],
        activeMapMolNo: null,
        moleculesDisplayObjectsKeys: this.moleculesRef.current.map(molecule => Object.keys(molecule.displayObjects).filter(key => molecule.displayObjects[key].length > 0)),
        moleculesCootBondsOptions: this.moleculesRef.current.map(molecule => molecule.cootBondsOptions),
        mapsCootContours: [],
        mapsContourLevels: [],
        mapsColours: [],
        mapsLitLines: [],
        mapsRadius: [],
        mapsIsDifference: [],
        origin: this.glRef.current.origin,
        backgroundColor: this.glRef.current.background_colour,
        atomLabelDepthMode: this.preferences.atomLabelDepthMode,
        ambientLight: this.glRef.current.light_colours_ambient,
        diffuseLight: this.glRef.current.light_colours_diffuse,
        lightPosition: this.glRef.current.light_positions,
        specularLight: this.glRef.current.light_colours_specular,
        fogStart: this.glRef.current.gl_fog_start,
        fogEnd: this.glRef.current.gl_fog_end,
        zoom: this.glRef.current.zoom,
        doDrawClickedAtomLines: this.glRef.current.doDrawClickedAtomLines,
        clipStart: (this.glRef.current.gl_clipPlane0[3] + 500) * -1,
        clipEnd: this.glRef.current.gl_clipPlane1[3] - 500,
        quat4: this.glRef.current.myQuat
    }

    return JSON.stringify(session)
}

MoorhenTimeCapsule.prototype.addModification = async function() {
    this.modificationCount += 1
    if (this.modificationCount >= this.modificationCountBackupThreshold) {
        this.busy = true
        this.modificationCount = 0
        const sessionString = await this.fetchSession()
        return this.createBackup(sessionString)
    }
}

MoorhenTimeCapsule.prototype.cleanupIfFull = async function() {
    const keys = await this.autoBackupsStorageInstance.keys()
    const sortedKeys = keys.filter(key => key.indexOf("backup-") == 0).sort((a,b)=>{return parseInt(a.substr(7))-parseInt(b.substr(7))}).reverse()
    if (sortedKeys.length >= this.maxBackupCount) {
        const toRemoveCount = sortedKeys.length - this.maxBackupCount
        const promises = sortedKeys.slice(-toRemoveCount).map(key => this.removeBackup(key, true))
        await Promise.all(promises)
    }
}

MoorhenTimeCapsule.prototype.createBackup = async function(value, name=null) {
    let storageInstance
    let key
    if(name) {
        key =`${name} (${Date.now()})`
        storageInstance = this.manualBackupsStorageInstance
    } else {
        key =`backup-${Date.now()}`
        storageInstance = this.autoBackupsStorageInstance
    }
    console.log(`Creating backup ${key} in local storage...`)
    try {
        await storageInstance.setItem(key, value)
         console.log("Successully created backup in time capsule")
         if(name !== null) {
            await this.cleanupIfFull()
         }
         this.busy = false
         return key
     } catch (err) {
         console.log(err)
     }
}

MoorhenTimeCapsule.prototype.retrieveBackup = async function(key, isAuto=false) {
    console.log(`Fetching backup ${key} from local storage...`)
    let storageInstance
    if(isAuto) {
        storageInstance = this.autoBackupsStorageInstance
    } else {
        storageInstance = this.manualBackupsStorageInstance
    }
    try {
         return await storageInstance.getItem(key)
     } catch (err) {
         console.log(err)
     }
}

MoorhenTimeCapsule.prototype.removeBackup = async function(key, isAuto=false) {
    console.log(`Removing backup ${key} from time capsule...`)
    let storageInstance
    if(isAuto) {
        storageInstance = this.autoBackupsStorageInstance
    } else {
        storageInstance = this.manualBackupsStorageInstance
    }
    try {
         await storageInstance.removeItem(key)
         console.log('Successully removed backup from time capsule')
     } catch (err) {
         console.log(err)
     }
}

MoorhenTimeCapsule.prototype.dropAllBackups = async function(isAuto=false) {
    console.log(`Removing all backups from time capsule...`)
    let storageInstance
    if(isAuto) {
        storageInstance = this.autoBackupsStorageInstance
    } else {
        storageInstance = this.manualBackupsStorageInstance
    }
    try {
         await storageInstance.clear()
         console.log('Successully removed all backup from time capsule')
     } catch (err) {
         console.log(err)
     }
}
