# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| 1.0.x | yes |

## Reporting a vulnerability

Please do not report security issues through public issues, pull requests or discussions.

Use GitHub's private reporting instead: on the repository page open **Security**, then **Report a vulnerability**. The report stays private until a fix is available.

Please include:

- the plugin version, shown in the Settings dialog under About
- the operating system, the KDE Plasma and Qt versions, and the `svn` client version
- what you observed and how to reproduce it
- the effect you consider possible

## What to expect

This is a project maintained in spare time. There is no guaranteed response time and no service level agreement. Reports are read and answered as time allows.

## Scope

The plugin runs on the machine of whoever installs it and drives the local Subversion command line client. There is no service operated by the project.

In scope are defects in the plugin, in the delivered build and install scripts and in the documented usage, for example an action that builds an unsafe `svn` command from a working copy path or a repository response.

Out of scope are:

- vulnerabilities in third-party components such as Qt, the KDE Frameworks, the `svn` client and the external diff and merge tool. Report those to the project concerned
- issues that require an already compromised account or an attacker who can already run code as the user
- the security of a Subversion server or repository, which is outside this plugin

## Security-relevant behaviour

The plugin never stores credentials of its own. Authentication is handled by the `svn` client and its configuration. The plugin passes working copy paths, URLs and messages to the `svn` client as separate process arguments rather than through a shell, so a path or message cannot inject a second command. Diagnostic output from the client is parsed, not executed.
