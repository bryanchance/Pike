constant replace_from=("\240 \n\r\t!\"#$%&'()*+,-./0123456789:;<=>?@[\]^_`{|}~���������������������������������")/"";
constant replace_to=({" "})*(sizeof(replace_from));


array(string) tokenize(string in)
{
  return (in/" ") - ({ "" });
}


string normalize(string in)
{
  in=lower_case(in);
  return replace(in, replace_from, replace_to);
}
