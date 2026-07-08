# 📚 CloudFS Documentation

Welcome to CloudFS documentation! Choose your path:

## 🆕 Getting Started (START HERE!)

**First time using CloudFS?**

1. Read [START_HERE.md](START_HERE.md) - Quick start in 2 minutes ⭐
1. Then [SETUP_GUIDE.md](SETUP_GUIDE.md) - Comprehensive setup

## 📖 Documentation by Use Case

### Using CloudFS

- **[Quick Start](START_HERE.md)** - Get up and running in 2 minutes
- **[Setup Guide](SETUP_GUIDE.md)** - Complete setup walkthrough
- **[SharePoint Integration](QUICKSTART_SHAREPOINT.md)** - SharePoint provider
  examples
- **[Build Instructions](BUILD_QUICKSTART.md)** - Build and troubleshoot

### Developing & Contributing

- **[Development Guide](DEVELOPMENT.md)** - Development workflow and code style
- **[How to Contribute](CONTRIBUTING_PT.md)** - Contributing guide (Portuguese)
- **[Commit Guidelines](COMMIT_PLAN.md)** - Git workflow and conventional
  commits
- **[Commitlint Setup](COMMITLINT_SETUP.md)** - Pre-commit hooks configuration
- **[Adding a Provider](adding_a_provider.md)** - Implement new cloud storage
  provider

### Submitting Patches

- **[Quick Overview](COMO_SUBMETER_PATCHES.md)** - 30-second overview
  (Portuguese) ⭐
- **[Detailed Guide](CONTRIBUTING_PT.md)** - Full contribution guide
  (Portuguese)
- **[Quick Reference](SUBMIT_PATCH_QUICK_REFERENCE.md)** - Commands & formats
  cheat sheet

### 🔐 Authentication & Enterprise

- **[Investigation Summary](INVESTIGATION_SUMMARY.md)** - Auth audit results &
  provider status ⭐ NEW
- **[Authentication Audit](AUTHENTICATION_AUDIT.md)** - Detailed provider
  analysis
- **[Implementation Roadmap](IMPLEMENTATION_ROADMAP.md)** - Fix plan for
  server-to-server auth

## 🛠️ Helper Scripts

Located in `scripts/` folder at project root:

| Script      | Command                                 | Purpose                                       |
| ----------- | --------------------------------------- | --------------------------------------------- |
| **Compile** | `scripts/compile_for_current_duckdb.sh` | Auto-compile for your DuckDB version          |
| **Setup**   | `scripts/setup_secrets.sh`              | Interactive secret/credential setup           |
| **Extract** | `scripts/extract_from_aca.sh`           | Extract SharePoint credentials from Azure ACA |
| **Run**     | `scripts/duckdb-cloudfs.sh`             | Wrapper to run DuckDB with -unsigned          |
| **Patch**   | `scripts/submit_patch.sh`               | Guide for submitting patches                  |

## 📚 Documentation Navigation

```
START_HERE.md
    ↓
Choose your path:
    ├─→ SETUP_GUIDE.md (detailed setup)
    │       ↓
    │   QUICKSTART_SHAREPOINT.md (provider examples)
    │       ↓
    │   INVESTIGATION_SUMMARY.md (if need CI/CD/automation)
    │       ↓
    │   AUTHENTICATION_AUDIT.md (detailed provider analysis)
    │
    ├─→ CONTRIBUTING_PT.md (want to contribute?)
    │       ↓
    │   COMO_SUBMETER_PATCHES.md (submit a patch)
    │       ↓
    │   SUBMIT_PATCH_QUICK_REFERENCE.md (commands)
    │
    └─→ DEVELOPMENT.md (developing CloudFS)
            ↓
        COMMIT_PLAN.md (commit guidelines)
            ↓
        IMPLEMENTATION_ROADMAP.md (if fixing auth)
            ↓
        adding_a_provider.md (add new provider)
```

## ✅ Common Tasks

### I want to...

| Task                            | Read                                                                                    |
| ------------------------------- | --------------------------------------------------------------------------------------- |
| Get started quickly             | [START_HERE.md](START_HERE.md)                                                          |
| Set up SharePoint access        | [SETUP_GUIDE.md](SETUP_GUIDE.md) + [QUICKSTART_SHAREPOINT.md](QUICKSTART_SHAREPOINT.md) |
| Build from source               | [BUILD_QUICKSTART.md](BUILD_QUICKSTART.md)                                              |
| Contribute code                 | [CONTRIBUTING_PT.md](CONTRIBUTING_PT.md)                                                |
| Submit a patch/PR               | [COMO_SUBMETER_PATCHES.md](COMO_SUBMETER_PATCHES.md)                                    |
| Develop a new feature           | [DEVELOPMENT.md](DEVELOPMENT.md)                                                        |
| Add a new cloud provider        | [adding_a_provider.md](adding_a_provider.md)                                            |
| Understand commits              | [COMMIT_PLAN.md](COMMIT_PLAN.md)                                                        |
| Use CloudFS in automation/CI-CD | [INVESTIGATION_SUMMARY.md](INVESTIGATION_SUMMARY.md)                                    |
| Implement server-to-server auth | [IMPLEMENTATION_ROADMAP.md](IMPLEMENTATION_ROADMAP.md)                                  |

## 🚀 Quick Links

- **[Project Main README](../README.md)** - Project overview

- **[GitHub Repository](https://github.com/trouchet/cloudfs)** - Source code

- **[DuckDB Docs](https://duckdb.org/docs/)** - DuckDB documentation

- [Agent Handover](../AGENT_HANDOVER.md) - Roadmap and tasks

- [Task Status](TASK1_STATUS.md) - Current progress

______________________________________________________________________

## 📂 Documentation Structure

```
docs/
├── README.md                    # This file - Documentation index
├── DEVELOPMENT.md               # Development workflow and guidelines
├── BUILD_QUICKSTART.md          # Build instructions
├── COMMIT_PLAN.md               # Git commit strategy
├── COMMITLINT_SETUP.md          # Pre-commit setup guide
├── TASK1_STATUS.md              # Current task status
└── adding_a_provider.md         # Provider implementation guide
```

______________________________________________________________________

## 🛠️ Development Workflow

### 1. Setup Environment

```bash
# Install development tools
make dev-setup

# Validate setup
make validate
```

### 2. Make Changes

```bash
# Create feature branch
git checkout -b feat/your-feature

# Make changes
vim src/core/your_file.cpp

# Format code
make format
```

### 3. Test & Commit

```bash
# Run tests
make test

# Run all checks
make check-all

# Commit with conventional format
git commit -m "feat(core): add your feature"
```

### 4. Submit PR

```bash
# Push branch
git push origin feat/your-feature

# Create PR on GitHub
```

See [DEVELOPMENT.md](DEVELOPMENT.md) for complete workflow.

______________________________________________________________________

## 📝 Document Types

### Guides

Step-by-step instructions for specific tasks:

- **BUILD_QUICKSTART.md** - Building the extension
- **adding_a_provider.md** - Adding cloud providers
- **COMMITLINT_SETUP.md** - Setting up commit validation

### References

Complete information about systems and workflows:

- **DEVELOPMENT.md** - Full development reference
- **COMMIT_PLAN.md** - Complete commit strategy

### Status

Current state of the project:

- **TASK1_STATUS.md** - Implementation status
- **AGENT_HANDOVER.md** - Roadmap and tasks

______________________________________________________________________

## 🔍 Finding What You Need

| I want to...           | Read this...                                 |
| ---------------------- | -------------------------------------------- |
| Build the extension    | [BUILD_QUICKSTART.md](BUILD_QUICKSTART.md)   |
| Contribute code        | [DEVELOPMENT.md](DEVELOPMENT.md)             |
| Add a provider         | [adding_a_provider.md](adding_a_provider.md) |
| Understand commits     | [COMMIT_PLAN.md](COMMIT_PLAN.md)             |
| Setup pre-commit hooks | [COMMITLINT_SETUP.md](COMMITLINT_SETUP.md)   |
| See current status     | [TASK1_STATUS.md](TASK1_STATUS.md)           |
| See roadmap            | [../AGENT_HANDOVER.md](../AGENT_HANDOVER.md) |

______________________________________________________________________

## 🤝 Contributing to Docs

Found a typo or want to improve the docs? Great!

1. Edit the relevant `.md` file
1. Follow the commit format: `docs(filename): your change`
1. Submit a PR

Example:

```bash
git commit -m "docs(development): clarify build instructions"
```

______________________________________________________________________

## 📞 Help & Support

- **Questions**: Open a
  [GitHub Discussion](https://github.com/yourusername/cloudfs/discussions)
- **Bug reports**: Open a
  [GitHub Issue](https://github.com/yourusername/cloudfs/issues)
- **Feature requests**: Open a
  [GitHub Issue](https://github.com/yourusername/cloudfs/issues)

______________________________________________________________________

**Happy coding! 🚀**
