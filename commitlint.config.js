module.exports = {
  extends: ['@commitlint/config-conventional'],
  rules: {
    'type-enum': [
      2,
      'always',
      [
        'feat',      // New feature
        'fix',       // Bug fix
        'docs',      // Documentation only
        'style',     // Code style changes (formatting, semicolons, etc)
        'refactor',  // Code refactoring
        'perf',      // Performance improvements
        'test',      // Adding or updating tests
        'build',     // Build system or external dependencies
        'ci',        // CI configuration changes
        'chore',     // Other changes that don't modify src or test files
        'revert'     // Revert a previous commit
      ]
    ],
    'scope-enum': [
      2,
      'always',
      [
        'core',              // Core filesystem/backend
        'table-functions',   // ls, stat, du functions
        'auth',              // Authentication providers
        'cache',             // Caching layer
        'http',              // HTTP client
        'providers',         // Cloud providers (generic)
        'onedrive',          // OneDrive provider
        'sharepoint',        // SharePoint provider
        'gdrive',            // Google Drive provider
        'dropbox',           // Dropbox provider
        'sftp',              // SFTP provider
        'vfs',               // VFS/agent provider
        'agent',             // Go agent service
        'extension',         // DuckDB extension wrapper
        'build',             // Build system
        'deps',              // Dependencies
        'docs',              // Documentation
        'tests',             // Test infrastructure
        'scripts'            // Shell scripts
      ]
    ],
    'scope-case': [2, 'always', 'lower-case'],
    'subject-empty': [2, 'never'],
    'subject-full-stop': [2, 'never', '.'],
    'subject-case': [
      2,
      'never',
      ['sentence-case', 'start-case', 'pascal-case', 'upper-case']
    ],
    'header-max-length': [2, 'always', 100],
    'body-leading-blank': [1, 'always'],
    'body-max-line-length': [2, 'always', 100],
    'footer-leading-blank': [1, 'always'],
    'footer-max-line-length': [2, 'always', 100]
  }
};
