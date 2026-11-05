const js = require('@eslint/js');

module.exports = [
  { ignores: ['client/scripts/vendors/**'] },
  js.configs.recommended,
  {
    files: ['client/**/*.js'],
    languageOptions: { ecmaVersion: 'latest', sourceType: 'script' },
    rules: { 'no-undef': 'off', 'no-unused-vars': 'off', 'no-console': 'warn' }
  }
];
