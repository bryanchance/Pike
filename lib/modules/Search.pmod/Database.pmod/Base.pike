//! @class Base class for Search Database implementations

//! Inserts a document in the database.
void insert_document(string uri, string title, string description, int last_changed,
		     int size, int mime_type, array words);

//! Removes a document from the database.
void remove_document(string uri);

//! Return a mapping with all the documents containing @variable word.
//! The index part of the mapping contains document URIs.
array(mapping) lookup_word(string word);

//! Return a mapping with all the documents containing one or more of @variable words. 
//! The index part of the mapping contains document URIs.
array(mapping) lookup_words_or(array(string) words);

//! Return a mapping with all the documents containing all of  @variable words
//! The index part of the mapping contains document URIs.
array(mapping) lookup_words_and(array(string) words);

//! Optimize the stored data.
void optimize();
