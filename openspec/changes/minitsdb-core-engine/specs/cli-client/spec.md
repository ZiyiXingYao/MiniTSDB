## ADDED Requirements

### Requirement: CLI SHALL support interactive mode with prompt
When started without a script file, the CLI SHALL enter interactive mode with a `minitsdb>` prompt, reading SQL statements line by line.

#### Scenario: Interactive query
- **WHEN** the user types "SELECT * FROM boiler_temp WHERE tag='BOILER-001' LATEST" at the prompt
- **THEN** the CLI SHALL display the result in a formatted table

### Requirement: CLI SHALL support batch mode from file
When started with `-f script.sql`, the CLI SHALL execute all SQL statements from the file and exit.

#### Scenario: Execute SQL script file
- **WHEN** running `minitsdb --host 127.0.0.1 --port 8086 -f script.sql`
- **THEN** the CLI SHALL execute each SQL statement in the file sequentially and output results

### Requirement: CLI SHALL support multiple output formats
The CLI SHALL support `--format table` (default), `--format csv`, and `--format json` output modes.

#### Scenario: JSON output format
- **WHEN** running `minitsdb --format json -e "SELECT * FROM boiler_temp LATEST"`
- **THEN** the CLI SHALL output results as a JSON array of objects
