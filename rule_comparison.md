# Constraint Rule Comparison
## Global
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| disabled               |✅|✅|✅|✅|✅|

## Numerics
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| const                  |✅|✅|✅|✅|✅|
| lt/lte/gt/gte          |✅|✅|✅|✅|✅|
| in/not_in              |✅|✅|✅|✅|✅|

## Bools
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| const                  |✅|✅|✅|✅|✅|

## Strings
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| const                  |✅|✅|✅|✅|✅|
| len/min\_len/max_len   |✅|✅|✅|✅|✅|
| min\_bytes/max\_bytes  |✅|✅|✅|✅|✅|
| pattern                |✅|✅|✅|✅|✅|
| prefix/suffix/contains |✅|✅|✅|✅|✅|
| contains/not_contains  |✅|✅|✅|✅|✅|
| in/not_in              |✅|✅|✅|✅|✅|
| email                  |✅|✅|❌|✅|✅|
| hostname               |✅|✅|✅|✅|✅|
| address                |✅|✅|✅|✅|✅|
| ip                     |✅|✅|✅|✅|✅|
| ipv4                   |✅|✅|✅|✅|✅|
| ipv6                   |✅|✅|✅|✅|✅|
| uri                    |✅|✅|❌|✅|✅|
| uri_ref                |✅|✅|❌|✅|✅|
| uuid                   |✅|✅|✅|✅|✅|
| well_known_regex       |✅|✅|✅|✅|✅|

## Bytes
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| const                  |✅|✅|✅|✅|✅|
| len/min\_len/max_len   |✅|✅|✅|✅|✅|
| pattern                |✅|✅|✅|✅|✅|
| prefix/suffix/contains |✅|✅|✅|✅|✅|
| in/not_in              |✅|✅|✅|✅|✅|
| ip                     |✅|✅|❌|✅|✅|
| ipv4                   |✅|✅|❌|✅|✅|
| ipv6                   |✅|✅|❌|✅|✅|

## Enums
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| const                  |✅|✅|✅|✅|✅|
| defined_only           |✅|✅|✅|✅|✅|
| in/not_in              |✅|✅|✅|✅|✅|

## Messages
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| skip                   |✅|✅|✅|✅|✅|
| required               |✅|✅|✅|✅|✅|

## Repeated
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| min\_items/max_items   |✅|✅|✅|✅|✅|
| unique                 |✅|✅|✅|✅|✅|
| items                  |✅|✅|❌|✅|✅|

## Maps
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| min\_pairs/max_pairs   |✅|✅|✅|✅|✅|
| no_sparse              |✅|✅|❌|❌|❌|
| keys                   |✅|✅|❌|✅|✅|
| values                 |✅|✅|❌|✅|✅|

## OneOf
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| required               |✅|✅|✅|✅|✅|

## WKT Scalar Value Wrappers
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| wrapper validation     |✅|✅|✅|✅|✅|

## WKT Any
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| required               |✅|✅|✅|✅|✅|
| in/not_in              |✅|✅|✅|✅|✅|

## WKT Duration
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| required               |✅|✅|✅|✅|✅|
| const                  |✅|✅|✅|✅|✅|
| lt/lte/gt/gte          |✅|✅|✅|✅|✅|
| in/not_in              |✅|✅|✅|✅|✅|

## WKT Timestamp
| Constraint Rule | Go | GoGo | C++ | Java | Python |
| ---| :---: | :---: | :---: | :---: | :---: |
| required               |✅|✅|❌|✅|✅|
| const                  |✅|✅|❌|✅|✅|
| lt/lte/gt/gte          |✅|✅|❌|✅|✅|
| lt_now/gt_now          |✅|✅|❌|✅|✅|
| within                 |✅|✅|❌|✅|✅|
