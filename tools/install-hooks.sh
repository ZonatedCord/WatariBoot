#!/bin/sh
# Da eseguire una volta dopo il clone: attiva gli hook versionati del progetto.
git config core.hooksPath .githooks && echo "Hook attivati (.githooks)."
