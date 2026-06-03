# 📚 CloudFS Documentation

Welcome to the CloudFS documentation! This directory contains all guides and
documentation for developing and using the CloudFS DuckDB extension.

## 📖 Table of Contents

### Getting Started

- **[Main README](../README.md)** - Project overview and quick start
- **[Build Quickstart](BUILD_QUICKSTART.md)** - Step-by-step build instructions
  and troubleshooting

### Development

- **[Development Guide](DEVELOPMENT.md)** - Complete development workflow, code
  style, and tools
- **[Commit Guidelines](COMMIT_PLAN.md)** - Git commit strategy and conventional
  commits
- **[Commitlint Setup](COMMITLINT_SETUP.md)** - Pre-commit hooks and validation
  setup

### Architecture & Extension

- **[Adding a Provider](adding_a_provider.md)** - How to implement a new cloud
  storage provider
- **[Task Status](TASK1_STATUS.md)** - Current task status and implementation
  details

### Project Management

- **[Agent Handover](../AGENT_HANDOVER.md)** - Project roadmap, tasks, and next
  steps

______________________________________________________________________

## 🚀 Quick Links

### For New Contributors

1. Read the [main README](../README.md) for project overview
1. Follow [Build Quickstart](BUILD_QUICKSTART.md) to compile
1. Review [Development Guide](DEVELOPMENT.md) for workflow
1. Check [Commit Guidelines](COMMIT_PLAN.md) before committing

### For Users

- [Main README](../README.md) - Usage examples and supported providers

### For Maintainers

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
