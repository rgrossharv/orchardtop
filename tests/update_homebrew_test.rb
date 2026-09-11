# SPDX-License-Identifier: Apache-2.0
require 'minitest/autorun'
require 'tmpdir'
require 'open3'
require 'rbconfig'

class UpdateHomebrewTest < Minitest::Test
  SCRIPT = File.expand_path('../scripts/update-homebrew.rb', __dir__)
  ORIGINAL = <<~'FORMULA'
    class Orchardtop < Formula
      url "https://github.com/rgrossharv/orchardtop.git",
          using: :git,
          tag: "v1.4.8",
          revision: "fbef0ab5a0b8eef1dabfd1c877283fc730119570"
      version "1.4.8"
      depends_on arch: :arm64
      def install
        system "make", "QUIET=true"
        bin.install "bin/orchardtop"
        bin.install "otop"
        (share / "orchardtop").install "themes"
      end
    end
  FORMULA

  def setup
    @dir = Dir.mktmpdir('orchardtop-formula-test')
    @formula = File.join(@dir, 'orchardtop.rb')
    File.write(@formula, ORIGINAL)
  end

  def teardown
    FileUtils.remove_entry(@dir)
  end

  def update(version = '1.4.10')
    Open3.capture3(RbConfig.ruby, SCRIPT, @formula, version, 'a' * 40)
  end

  def test_update_and_resume_preserve_installation
    _, error, result = update
    assert result.success?, error
    changed = File.read(@formula)
    assert_includes changed, 'version "1.4.10"'
    assert_includes changed, 'tag: "v1.4.10"'
    assert_includes changed, 'CXX=#{Formula["gcc@15"].opt_bin}/g++-15'
    assert_includes changed, '(share / "orchardtop").install "themes"'
    assert_includes changed, 'bin.install "otop"'
    assert_equal 1, changed.scan('depends_on "gcc@15"').length
    _, error, result = update
    assert result.success?, error
    assert_equal changed, File.read(@formula), 'A retry must not duplicate the compiler argument'
    _, error, result = Open3.capture3(RbConfig.ruby, '-c', @formula)
    assert result.success?, error
  end

  def test_refuse_downgrade_without_writing
    _, _, result = update('1.4.7')
    refute result.success?
    assert_equal ORIGINAL, File.read(@formula)
  end

  def test_refuse_unknown_build_command_without_writing
    changed = ORIGINAL.sub('system "make", "QUIET=true"', 'system "cmake", "--build", "."')
    File.write(@formula, changed)
    _, _, result = update
    refute result.success?
    assert_equal changed, File.read(@formula)
  end
end
