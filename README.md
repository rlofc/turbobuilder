![TURBUBUILDER BANNER](/art/banner.png)

# TURBOBUILDER

An experimental 80's inspired data entry and retrieval tool.

![TURBUBUILDER GIF](/art/merchant.gif)

## Current state

*EXPERIMENTAL* - use with caution. There are many features still missing for
TURBOBUILDER to be fully usable.

## Declarative Application Development

TURBOBUILDER is a declarative, model-driven, single-user application generator.
TURBOBUILDER generates simple data management applications using a declarative
entity model. The model is currently read from a text file - but the plan is
to use TURBOBUILDER itself to define it.

## But why??!

I started experimenting with building C projects using Tup, then things went
out of hand.

## Model Language

The model language supports the following capabilities:

* Entities (tables) definition
* Fields (columns) definition (basic types: string, integer, real, date)
* 1-M (one-to-many) entity relations
* Simple calculated fields (transient)
* Basic i18n using labels translation definitions.

### Sample

```
App = application {
    title: "SAMPLE"
}

Employee = entity {
    Id = field { type: string; size: 10; listed: true; }
    FirstName = field { type: string; size: 16; listed: true; }
    LastName = field { type: string; size: 16; listed: true; }
}

Company = entity {
    Name = field { type: string; size: 20; listed: true; }
    Employees = relation { ref: Employee.Id; }
}

English = translation {
    Id = "Employee Id";
    Company = "Employer";
}

```
