const { app, BrowserWindow, ipcMain } = require('electron')
const path = require('path')
const net = require('net')

let mainWindow

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 780,
    height: 620,
    minWidth: 700,
    minHeight: 500,
    resizable: true,
    titleBarStyle: 'hiddenInset',
    backgroundColor: '#080808',
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false
    }
  })
  mainWindow.loadFile(path.join(__dirname, 'index.html'))
}

app.whenReady().then(createWindow)
app.on('window-all-closed', () => app.quit())

// Send single command
ipcMain.handle('send-command', async (event, cmd) => {
  return new Promise((resolve) => {
    const client = net.createConnection('/tmp/spotpitch.sock', () => {
      client.write(cmd)
    })
    client.on('data', () => { client.destroy(); resolve('ok') })
    client.on('error', () => resolve('error'))
    setTimeout(() => { client.destroy(); resolve('timeout') }, 300)
  })
})

// Send multiple commands in one connection
ipcMain.handle('send-batch', async (event, cmds) => {
  return new Promise((resolve) => {
    const client = net.createConnection('/tmp/spotpitch.sock', () => {
      // Send all commands joined by newline
      client.write(cmds.join('\n'))
    })
    client.on('data', () => { client.destroy(); resolve('ok') })
    client.on('error', () => resolve('error'))
    setTimeout(() => { client.destroy(); resolve('timeout') }, 500)
  })
})
