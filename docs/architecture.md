# Architecture

The code is split into a **backend** (`be`), which knows nothing about the user interface, and a **frontend** (`fe`), organised as model (`m`), view (`v`) and controller (`c`). The frontend depends on the backend only through the interfaces in `be/service`; the concrete services are created in one place, `App.java`, and handed to the controllers.

```
lemuri/
├── App.java              startup: creates the services and wires them to the controllers
├── be/                   backend, no JavaFX
│   ├── dto/              plain data: Host, Resources, Delivery, Peer…
│   ├── service/          interfaces: HostRepository, Vault, MetricsService,
│   │                     ProbeService, SharingService, LanService, Transport…
│   ├── storage/          JsonHostRepository (hosts.json), FileVault (the encrypted vault)
│   ├── terminal/         SSH transport, jump hosts, known host keys, SFTP transfers
│   ├── monitoring/       metrics and reachability probe
│   ├── sharing/          encrypted envelopes, contacts, envelopes as text, local network
│   └── util/             folder paths, operating system
└── fe/                   frontend, JavaFX
    ├── m/                model: observable state (AppState), sessions, tasks
    ├── c/                controllers: HostController, SessionController, MonitorController,
    │                     SharingController, LanController; AppContext groups them
    └── v/                views: main window, tree, dialogs; sections/ holds the central views,
                          terminal/ the terminal (WebView + xterm.js)
```

Services run their work on background threads and report back through callbacks or listeners; the controllers move the results onto the JavaFX thread and into `AppState`, which the views observe.

Code and comments are in English. Data formats keep their original field names (for example in `hosts.json`, the vault and the sharing protocol), so existing files stay readable.

| Other paths | Content |
|---|---|
| `src/main/resources/lemuri/` | stylesheet, xterm.js |
