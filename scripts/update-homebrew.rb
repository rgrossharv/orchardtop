# SPDX-License-Identifier: Apache-2.0
# Preserve the source formula's installation and theme logic when updating it.
require 'rubygems'
path, version, revision = ARGV
abort 'Usage: update-homebrew.rb FORMULA VERSION COMMIT' unless ARGV.length == 3
abort 'Invalid version/commit' unless version.match?(/\A\d+\.\d+\.\d+\z/) && revision.match?(/\A[0-9a-f]{40}\z/)
formula = File.read(path)
abort 'Unexpected source repository' unless formula.include?('url "https://github.com/rgrossharv/orchardtop.git"')
old_version = formula[/^  version "([^"]+)"$/, 1] or abort 'Missing version'
abort 'Refusing to downgrade the tap' if Gem::Version.new(old_version) > Gem::Version.new(version)
def replace_once(text, pattern, replacement)
  abort "Unexpected formula structure: #{pattern}" unless text.scan(pattern).length == 1
  text.sub!(pattern) { replacement }
end
replace_once(formula, /tag: "v[^"]+"/, "tag: \"v#{version}\"")
replace_once(formula, /revision: "[0-9a-f]+"/, "revision: \"#{revision}\"")
replace_once(formula, /^  version "[^"]+"$/, "  version \"#{version}\"")
unless formula.include?('depends_on "gcc@15"')
  replace_once(formula, /^  depends_on arch: :arm64$/, "  depends_on arch: :arm64\n  depends_on \"gcc@15\"")
end
replace_once(formula, /^    system "make", "QUIET=true"(?:, "CXX=#\{Formula\["gcc@15"\]\.opt_bin\}\/g\+\+-15")?$/,
             '    system "make", "QUIET=true", "CXX=#{Formula["gcc@15"].opt_bin}/g++-15"')
File.write(path, formula)
