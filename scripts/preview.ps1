# RITA's screens on the PC, no board needed. See scripts\preview\preview.py.
python (Join-Path $PSScriptRoot "preview\preview.py") @args
exit $LASTEXITCODE
