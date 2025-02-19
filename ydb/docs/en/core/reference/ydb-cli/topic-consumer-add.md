# Adding a topic consumer

You can use the `topic consumer add` command to add a consumer for a [previously created](topic-create.md) topic.

General format of the command:

```bash
{{ ydb-cli }} [global options...] topic consumer add [options...] <topic-path>
```

* `global options`: [Global parameters](commands/global-options.md).
* `options`: [Parameters of the subcommand](#options).
* `topic-path`: Topic path.

View the description of the add consumer command:

```bash
{{ ydb-cli }} topic consumer add --help
```

## Parameters of the subcommand {#options}

| Name | Description |
---|---
| `--consumer-name VAL` | Name of the consumer to be added. |
| `--starting-message-timestamp VAL` | Time in [UNIX timestamp](https://en.wikipedia.org/wiki/Unix_time) format. Consumption starts as soon as the first [message](../../concepts/topic.md#message) is received after the specified time. If the time is not specified, consumption will start from the oldest message in the topic. |

## Examples {#examples}

{% include [ydb-cli-profile](../../_includes/ydb-cli-profile.md) %}

Create a consumer with the `my-consumer` name for the [previously created](topic-create.md) `my-topic` topic. Consumption will start as soon as the first message is received after August 15, 2022 13:00:00 GMT:

```bash
{{ ydb-cli }} -p db1 topic consumer add \
  --consumer-name my-consumer \
  --starting-message-timestamp 1660568400 \
  my-topic
```

Make sure the consumer was created:

```bash
{{ ydb-cli }} -p db1 scheme describe my-topic
```

Result:

```text
RetentionPeriod: 2 hours
PartitionsCount: 2
SupportedCodecs: RAW, GZIP

Consumers:
┌──────────────┬─────────────────┬───────────────────────────────┬───────────┐
| ConsumerName | SupportedCodecs | ReadFrom                      | Important |
├──────────────┼─────────────────┼───────────────────────────────┼───────────┤
| my-consumer  | RAW, GZIP       | Mon, 15 Aug 2022 16:00:00 MSK | 0         |
└──────────────┴─────────────────┴───────────────────────────────┴───────────┘
```
