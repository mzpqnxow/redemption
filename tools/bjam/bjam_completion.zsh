_bjam_completion () {
  reply=($(
    sed -n -E '
    /^ *(test-run|unit-test|exe|install|alias|lib|make) /{
      s/^ *[^ ]+\s+([-_.a-zA-Z0-9]+).*/\1/
      H
      /_src$/!p
    }
    /^ *test-canonical /{
        s#^.+/([^.]+)\.h.*#test_\1#p
    }' Jamroot
    
    sed -n -E '/^alias tests/!{
      /^(exe|alias|lib) /{
        s/^[^ ]+\s+([-_a-zA-Z0-9]+) .*/\1/p
      } 
    }' targets.jam 2>/dev/null
  ))
}

compctl \
  -K _bjam_completion -M 'r:|[_[:lower:]]=** r:|=*' \
  - 's[cxxflags=]' -k '(gcc clang)' \
  -- bjam

compctl -x 's[cxxflags=]' -k '(gcc clang)' aaa
