# Contributing

Thanks for wanting to help!

- **Bugs and ideas**: open an issue. For bugs, include your OS, what you did, what you expected and what happened.
- **Code**: open a pull request against `main`. Keep it focused on one change, and make sure `./mvnw verify` and `./mvnw spotbugs:check` pass. A SpotBugs finding that is harmless goes in `spotbugs-exclude.xml`, as narrow as possible and with the reason.
- **Style**: code and comments are in English; please keep new code consistent with the file around it.
- **Dependencies**: if you add or update one, run `./licenses.sh` to regenerate `THIRD-PARTY-NOTICES.md` (it stops if the new dependency has no license text in `licenses/`). CI checks that the file is up to date.
- **Texts and translations**: every text of the interface is in `src/main/resources/lemuri/i18n/messages.properties` (English) and `messages_it.properties` (Italian), read with `I18n.t("key", values…)`. Values go through `MessageFormat`: write an apostrophe twice (`can''t`). To add a language, copy `messages.properties` to `messages_<code>.properties`, translate it and add the code to `I18n.LANGUAGES`; the tests check that every language has the same keys and placeholders.
- **Interface tests**: the classes ending in `UiTest` (in `src/test/java/lemuri/fe/v/`) drive the real main window with TestFX on Monocle's headless screen: no window opens on your desktop and CI needs no display. They extend `UiTest`, which builds a made-up world (hosts and vault in memory, fake metrics, sessions with fake transports, a fake home). Run one class with `./mvnw test -Dtest=SessionsUiTest`. A change you can see in the window deserves one.
- **Other distributions**: `bash src/test/distros/run.sh` (Docker needed; `DOCKER="sudo docker"` if your user is not in the docker group) starts SSH servers of seven distributions in containers, on 127.0.0.1 only, runs `DistrosTest` against them (sign-in with password and key, shell, metrics, updates and reboot, SFTP, jump host, `lemuri -s`) and removes them. A weekly GitHub Action runs it too.
- **Layers**: code in `be` must not use JavaFX or anything in `fe`; the frontend reaches the backend only through the interfaces in `be/service` (see [docs/architecture.md](docs/architecture.md)).
- **Changelog**: a change users will notice gets a line under *Unreleased* in [CHANGELOG.md](CHANGELOG.md).
- **Releases**: set the version in `pom.xml`, rename *Unreleased* in `CHANGELOG.md` to the version with its date, commit, then push a tag like `v0.2.0`. The Release workflow checks everything again and publishes the release with that section of the changelog as notes.
- **Security issues**: see [SECURITY.md](SECURITY.md), not public issues.

## Running

```sh
./mvnw javafx:run
```

Requires JDK 21 and `unzip`. Without a server at hand, a local SSH server (`sshd` on localhost) is enough to try most features.
